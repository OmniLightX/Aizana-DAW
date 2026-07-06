#include "PluginHostBridge.h"
#include "OobAudioMarshal.h"

#include <cstring>

namespace aizana
{
    void PluginHostBridge::run (SharedAudioRing& ring,
                                juce::AudioProcessor& plugin,
                                const std::function<bool()>& shouldExit)
    {
        const uint32_t stride = ring.maxBlockSamples();

        // Reused across blocks — clearing keeps storage, so no per-block allocation.
        juce::MidiBuffer midi;
        midi.ensureSize (SharedAudioRing::kMidiRegionBytes);

        ring.runChildLoop (
            [&plugin, &midi, &ring, stride] (float* data, uint32_t ch, uint32_t n)
            {
                const int numCh = (int) juce::jmin<uint32_t> (ch, (uint32_t) kMaxChannels);

                // E5.4: apply the host's queued parameter changes BEFORE the block — that is
                // what makes TE automation drive the child plugin. Zero the count afterwards
                // so a block without fresh host writes can't re-apply stale events.
                {
                    uint8_t* pr = ring.paramRegion();
                    uint32_t pc = 0;
                    std::memcpy (&pc, pr, 4);
                    pc = juce::jmin (pc, SharedAudioRing::kMaxParamEvents);

                    const auto& params = plugin.getParameters();
                    const uint8_t* p = pr + 4;
                    for (uint32_t i = 0; i < pc; ++i, p += 8)
                    {
                        uint32_t idx = 0; float v = 0.0f;
                        std::memcpy (&idx, p,     4);
                        std::memcpy (&v,   p + 4, 4);
                        if ((int) idx < params.size())
                            params[(int) idx]->setValue (juce::jlimit (0.0f, 1.0f, v));
                    }

                    const uint32_t zero = 0;
                    std::memcpy (pr, &zero, 4);
                }

                // Planar: channel c starts at data + c * stride. Wrap the shared region in an
                // AudioBuffer that REFERS to those pointers (no copy) and process in place.
                float* chans[kMaxChannels];
                for (int c = 0; c < numCh; ++c)
                    chans[c] = data + (size_t) c * (size_t) stride;

                juce::AudioBuffer<float> buffer (chans, numCh, (int) n);

                // E5.4: MIDI in from the host, plugin output MIDI back out. Zero the inbound
                // count after consuming so it can't replay if the host skips a write.
                midi.clear();
                OobAudioMarshal::decodeMidi (ring.midiInRegion(), SharedAudioRing::kMidiRegionBytes, midi);
                {
                    const uint32_t zero = 0;
                    std::memcpy (ring.midiInRegion(), &zero, 4);
                }

                plugin.processBlock (buffer, midi);

                OobAudioMarshal::encodeMidi (ring.midiOutRegion(), SharedAudioRing::kMidiRegionBytes, midi);
            },
            shouldExit);
    }
}
