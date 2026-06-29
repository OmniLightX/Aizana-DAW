#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedAudioRing.h"

#include <functional>

/**
 * PluginHostBridge — the CHILD-side pump of out-of-process plugin hosting (E5.2).
 *
 * Runs in the plugin-host child process. It owns the loop that, for every audio block the
 * host hands across the SharedAudioRing, wraps the shared planar region into a juce::AudioBuffer
 * and drives the real plugin's processBlock — the actual "a plugin runs in another process"
 * mechanism. The host proxy (E5.3) writes input into the ring, waits, and reads output back.
 *
 * The shared audio region is PLANAR with a fixed per-channel stride of ring.maxBlockSamples():
 *   channel c lives at audioData() + c * maxBlockSamples(). The host must use the same stride.
 *
 * Threading: this is the child's pump thread — NOT a real audio device callback, but we still
 * keep it allocation/lock free per block (the AudioBuffer wraps existing pointers, the MidiBuffer
 * is reused). The plugin must already be prepared (prepareToPlay) for ring.maxBlockSamples().
 */
namespace cloudes
{
    class PluginHostBridge
    {
    public:
        // Channel ceiling for the stack-allocated pointer array (no per-block heap use).
        static constexpr int kMaxChannels = 32;

        /** Pump `ring` until shouldExit() returns true, running `plugin.processBlock` in place
            over the shared audio region. `plugin` must be prepared for ring.maxBlockSamples(). */
        static void run (SharedAudioRing& ring,
                         juce::AudioProcessor& plugin,
                         const std::function<bool()>& shouldExit);
    };
}
