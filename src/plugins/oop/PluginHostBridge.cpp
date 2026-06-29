#include "PluginHostBridge.h"

namespace cloudes
{
    void PluginHostBridge::run (SharedAudioRing& ring,
                                juce::AudioProcessor& plugin,
                                const std::function<bool()>& shouldExit)
    {
        const uint32_t stride = ring.maxBlockSamples();

        // Reused across blocks — clearing keeps storage, so no per-block allocation. (MIDI
        // across the boundary is E5.4; for now the plugin sees an empty input MIDI buffer.)
        juce::MidiBuffer midi;

        ring.runChildLoop (
            [&plugin, &midi, stride] (float* data, uint32_t ch, uint32_t n)
            {
                const int numCh = (int) juce::jmin<uint32_t> (ch, (uint32_t) kMaxChannels);

                // Planar: channel c starts at data + c * stride. Wrap the shared region in an
                // AudioBuffer that REFERS to those pointers (no copy) and process in place.
                float* chans[kMaxChannels];
                for (int c = 0; c < numCh; ++c)
                    chans[c] = data + (size_t) c * (size_t) stride;

                juce::AudioBuffer<float> buffer (chans, numCh, (int) n);
                midi.clear();
                plugin.processBlock (buffer, midi);
            },
            shouldExit);
    }
}
