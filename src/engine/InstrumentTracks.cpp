#include "InstrumentTracks.h"

namespace aizana
{
    namespace
    {
        using TimePos = tracktion::core::TimePosition;
        using TimeRng = tracktion::core::TimeRange;

        /** Eine feste Quell-Kennung fuer alles, was von der Oberflaeche kommt —
            TE unterscheidet daran MPE-Quellen. */
        te::MPESourceID keyboardSourceID()
        {
            static const te::MPESourceID id = te::createUniqueMPESourceID();
            return id;
        }
    }

    bool isInstrumentTrack (const te::Track& track)
    {
        if (track.state.getProperty (kTrackTypeProp).toString() == kInstrumentTag)
            return true;

        // Rueckfall fuer Sessions von vor dieser Aenderung: dort gibt es das
        // Merkmal nicht, wohl aber MIDI-Clips. Ohne diesen Zweig wuerden alte
        // MIDI-Spuren beim Oeffnen ihren Eingang und ihr Verhalten verlieren.
        if (auto* clipTrack = dynamic_cast<const te::ClipTrack*> (&track))
            for (auto* clip : clipTrack->getClips())
                if (dynamic_cast<te::MidiClip*> (clip) != nullptr)
                    return true;

        return false;
    }

    void markAsInstrumentTrack (te::Track& track)
    {
        track.state.setProperty (kTrackTypeProp, kInstrumentTag, nullptr);
    }

    te::MidiInputDevice* getOrCreateKeyboardDevice (te::Engine& engine)
    {
        auto& dm = engine.getDeviceManager();

        auto findIt = [&dm]() -> te::MidiInputDevice*
        {
            for (auto& dev : dm.getMidiInDevices())
                if (dev != nullptr && dev->getName() == kKeyboardDeviceName)
                    return dev.get();
            return nullptr;
        };

        if (auto* existing = findIt())
            return existing;

        // Anlegen darf fehlschlagen (headless, keine Geraete) — dann liefert die
        // Suche danach schlicht nichts und der Aufrufer faellt zurueck.
        dm.createVirtualMidiDevice (kKeyboardDeviceName);
        auto* created = findIt();

        if (created != nullptr)
            created->setEnabled (true);

        return created;
    }

    bool sendKeyboardNoteOn (te::Engine& engine, int pitch, int velocity)
    {
        auto* dev = getOrCreateKeyboardDevice (engine);
        if (dev == nullptr)
            return false;

        dev->handleIncomingMidiMessage (
            juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)),
            keyboardSourceID());
        return true;
    }

    bool sendKeyboardNoteOff (te::Engine& engine, int pitch)
    {
        auto* dev = getOrCreateKeyboardDevice (engine);
        if (dev == nullptr)
            return false;

        dev->handleIncomingMidiMessage (juce::MidiMessage::noteOff (1, pitch), keyboardSourceID());
        return true;
    }

    te::InputDeviceInstance* findKeyboardInstance (te::Edit& edit)
    {
        auto* dev = getOrCreateKeyboardDevice (edit.engine);
        if (dev == nullptr)
            return nullptr;

        for (auto* instance : edit.getAllInputDevices())
            if (&instance->getInputDevice() == dev)
                return instance;

        return nullptr;
    }

    bool armKeyboardForTrack (te::AudioTrack& track)
    {
        auto* instance = findKeyboardInstance (track.edit);
        if (instance == nullptr)
            return false;

        // Ziel setzen, falls es noch nicht steht. `setTarget` feuert einen
        // AsyncUpdate (I-08) — deshalb wird der Aufnahme-Schalter unten NOCH
        // EINMAL asynchron nachgezogen, sonst raeumt der Update ihn wieder ab.
        if (! te::isOnTargetTrack (*instance, track, 0))
            if (! instance->setTarget (track.itemID, true, nullptr).has_value())
                return false;

        instance->setRecordingEnabled (track.itemID, true);
        return true;
    }

    void routeKeyboardToTrack (te::AudioTrack& track)
    {
        auto& edit = track.edit;
        auto* dev  = getOrCreateKeyboardDevice (edit.engine);
        if (dev == nullptr)
            return;

        // Monitoring AN.
        //
        // ⚠️ Die Erklaerung fuer ein Hin und Her, das den User zweimal Ton
        // gekostet hat — hier festgehalten, damit es nicht ein drittes Mal
        // passiert:
        //
        //   * Solange `setTarget` FEHLSCHLUG (die Geraete-Instanz existierte
        //     nach dem Anlegen des virtuellen Geraets noch nicht), lag auf der
        //     Spur gar kein Eingang. `injectLiveMidiMessage` war dann der
        //     einzige Weg — und er funktionierte.
        //   * Seit die Zuweisung zuverlaessig klappt (nachgereicht per
        //     callAsync, plus beim Scharfschalten), hat die Spur einen echten
        //     MIDI-Eingang. Damit haengt der hoerbare Weg an TEs Monitoring —
        //     und das stand hier auf OFF. Ergebnis: wieder kein Ton.
        //
        // Also: das GERAET macht hoerbar (Monitoring automatic) UND nimmt auf.
        // Ein Weg statt zwei halben. LiveMidiPreview injiziert nur noch dort,
        // wo es keinen Geraete-Eingang gibt (Audiospur mit Instrument,
        // Vorhoeren im Piano Roll) — sonst klaenge jede Note doppelt.
        dev->setMonitorMode (te::InputDevice::MonitorMode::automatic);

        // ⚠️ ZEITPUNKT: wurde das Geraet gerade erst angelegt, laeuft TEs
        // `rescanMidiDeviceList` noch — die INSTANZ zu diesem Geraet existiert
        // im Edit dann noch gar nicht, und ein sofortiges setTarget liefe ins
        // Leere (genau der Zustand, in dem "Ton ja, Aufnahme nein" entsteht).
        // Deshalb die Zuweisung nachreichen, sobald die Message-Schlange durch
        // ist. Der Aufnahme-Schalter folgt beim Scharfschalten ohnehin noch
        // einmal (armKeyboardForTrack).
        te::EditItemID id = track.itemID;
        juce::MessageManager::callAsync ([&edit, id]
        {
            for (auto* t : te::getAudioTracks (edit))
            {
                if (t->itemID != id)
                    continue;

                if (auto* instance = findKeyboardInstance (edit))
                    if (! te::isOnTargetTrack (*instance, *t, 0))
                        instance->setTarget (t->itemID, true, nullptr);

                break;
            }
        });
    }

    te::AudioTrack* createInstrumentTrack (te::Edit& edit,
                                           const juce::String& trackName,
                                           std::function<void (te::AudioTrack&)> insertInstrument)
    {
        auto& um = edit.getUndoManager();
        um.beginNewTransaction ("Instrumentenspur hinzufuegen");

        // Immer ans ENDE (I-06): sonst verschieben sich alle Spurnummern.
        auto* track = dynamic_cast<te::AudioTrack*> (
            edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr).get());

        if (track == nullptr)
            return nullptr;

        track->setSoloIsolate (false);          // I-06: auf JEDER neuen Spur
        markAsInstrumentTrack (*track);

        if (trackName.isNotEmpty())
            track->setName (trackName);

        if (insertInstrument != nullptr)
            insertInstrument (*track);

        // Ein leerer MIDI-Clip ueber vier Takte: ohne ihn hat die Spur nichts,
        // was sich doppelklicken liesse, und der Piano Roll bleibt unerreichbar.
        // (Genau daran ist der User haengengeblieben: "Wo finde ich denn den
        // MIDI-Clip?") Aufnehmen erzeugt spaeter eigene Clips daneben.
        const double bpm = edit.tempoSequence.getNumTempos() > 0
                             ? edit.tempoSequence.getTempo (0)->getBpm() : 120.0;
        const int beatsPerBar = edit.tempoSequence.getNumTimeSigs() > 0
                                  ? edit.tempoSequence.getTimeSig (0)->numerator.get() : 4;
        const double clipLen = 60.0 / bpm * (double) beatsPerBar * 4.0;

        te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0),
                                        TimePos::fromSeconds (clipLen)),
                               tracktion::core::TimeDuration() };

        track->insertNewClip (te::TrackItem::Type::midi, "MIDI", pos, nullptr);

        routeKeyboardToTrack (*track);

        return track;
    }
}
