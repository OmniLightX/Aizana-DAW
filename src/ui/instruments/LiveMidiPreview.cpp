#include "LiveMidiPreview.h"
#include "engine/InstrumentTracks.h"

namespace aizana
{
    namespace
    {
        te::AudioTrack* asAudioTrack (te::Track* t)
        {
            return dynamic_cast<te::AudioTrack*> (t);
        }

        /** Eine feste Quell-Kennung fuer alles, was aus der Oberflaeche kommt.
            TE unterscheidet damit Quellen (fuer MPE); eine pro Prozess genuegt. */
        te::MPESourceID uiSourceID()
        {
            static const te::MPESourceID id = te::createUniqueMPESourceID();
            return id;
        }
    }

    // ── Zwei Wege, und der erste ist der wichtige ────────────────────────────
    //
    // 1. UEBER DAS VIRTUELLE KEYBOARD-GERAET, wenn die Spur es als Eingang hat.
    //    Nur so kommt das Gespielte am AUFNAHMEPFAD vorbei. Vorher ging alles
    //    ausschliesslich ueber injectLiveMidiMessage — man hoerte die Noten,
    //    aber die Aufnahme blieb leer, ohne jede Fehlermeldung (User-Befund
    //    2026-08-15). In Logic ist das Musical-Typing-Fenster ausdruecklich eine
    //    vollwertige Aufnahmequelle.
    //
    // 2. DIREKT IN DEN WIEDERGABE-GRAPHEN, wenn es das Geraet nicht gibt oder
    //    die Spur es nicht hoert (kein MIDI-System, Audiospur mit Instrument,
    //    Vorhoeren im Piano Roll). Dann hoert man wenigstens etwas — lieber
    //    Vorhoeren ohne Aufnahme als Stille ohne Hinweis.

    static bool trackListensToKeyboard (te::Track* track)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return false;

        auto* dev = getOrCreateKeyboardDevice (at->edit.engine);
        if (dev == nullptr)
            return false;

        for (auto* instance : at->edit.getAllInputDevices())
            if (&instance->getInputDevice() == dev && te::isOnTargetTrack (*instance, *at, 0))
                return true;

        return false;
    }

    void sendLivePreviewNoteOn (te::Track* track, int pitch, int velocity)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        if (trackListensToKeyboard (track)
            && sendKeyboardNoteOn (at->edit.engine, pitch, velocity))
            return;

        at->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch,
                                       (juce::uint8) juce::jlimit (1, 127, velocity)),
                                   uiSourceID());
    }

    void sendLivePreviewNoteOff (te::Track* track, int pitch)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        if (trackListensToKeyboard (track) && sendKeyboardNoteOff (at->edit.engine, pitch))
            return;

        at->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, pitch), uiSourceID());
    }

    void sendLivePreviewAllNotesOff (te::Track* track)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        // Einzelne Note-Offs statt "all notes off" (CC 123): nicht jedes Instrument
        // wertet den Controller aus, eine haengende Note ist aber das Schlimmste,
        // was hier passieren kann.
        for (int p = 0; p < 128; ++p)
            at->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, p), uiSourceID());
    }
}
