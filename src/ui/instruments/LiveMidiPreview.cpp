#include "LiveMidiPreview.h"

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

    void sendLivePreviewNoteOn (te::Track* track, int pitch, int velocity)
    {
        if (auto* at = asAudioTrack (track))
            at->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch,
                                           (juce::uint8) juce::jlimit (1, 127, velocity)),
                                       uiSourceID());
    }

    void sendLivePreviewNoteOff (te::Track* track, int pitch)
    {
        if (auto* at = asAudioTrack (track))
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
