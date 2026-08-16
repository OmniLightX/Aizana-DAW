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

    // ── Genau EIN Weg, je nachdem ob die Spur einen Eingang hat ──────────────
    //
    // Hat die Spur das virtuelle Keyboard als Eingang, geht die Note DORTHIN:
    // TEs Monitoring macht sie hoerbar, und der Aufnahmepfad sieht sie. Das ist
    // der vollstaendige Weg.
    //
    // Hat sie keinen (Audiospur mit Instrument, Vorhoeren im Piano Roll), wird
    // direkt in den Wiedergabe-Graphen eingespeist — dann hoert man wenigstens
    // etwas, auch wenn nichts aufgenommen wird.
    //
    // NICHT BEIDES: sonst klingt jede Note doppelt.

    static bool trackListensToKeyboard (te::AudioTrack* at)
    {
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

        if (trackListensToKeyboard (at) && sendKeyboardNoteOn (at->edit.engine, pitch, velocity))
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

        if (trackListensToKeyboard (at) && sendKeyboardNoteOff (at->edit.engine, pitch))
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
