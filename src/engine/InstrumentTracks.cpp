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

    void routeKeyboardToTrack (te::AudioTrack& track)
    {
        auto& edit = track.edit;
        auto* dev  = getOrCreateKeyboardDevice (edit.engine);
        if (dev == nullptr)
            return;

        for (auto* instance : edit.getAllInputDevices())
        {
            if (&instance->getInputDevice() != dev)
                continue;

            // setTarget feuert einen AsyncUpdate — alles Weitere gehoert
            // deshalb hinter callAsync (I-08). Hier reicht das Ziel selbst;
            // Monitoring setzen wir direkt am Geraet, das ist unabhaengig davon.
            auto result = instance->setTarget (track.itemID, true, nullptr);
            if (! result.has_value())
                return;

            break;
        }

        // Ohne Monitoring hoert man das eigene Spiel nur bei laufender Aufnahme —
        // in Logic und Studio One hoert die ausgewaehlte Instrumentenspur immer mit.
        dev->setMonitorMode (te::InputDevice::MonitorMode::automatic);
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
