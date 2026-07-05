#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedAudioRing.h"

/**
 * OobAudioMarshal — the HOST side of out-of-process audio hosting (E5.3a).
 *
 * The counterpart to PluginHostBridge (child side). Called from the host's processBlock:
 * copies an AudioBuffer into the SharedAudioRing, round-trips it to the child plugin, and
 * copies the processed output back. If the child hangs or crashes (any non-ok ring status),
 * the buffer is SILENCED — a dead plugin must never leak stale audio or garbage into the mix,
 * and the host audio thread is never blocked beyond the timeout. That bounded-silence is the
 * whole hang/crash-immunity payoff of E5.
 *
 * Audio-thread safe: only memcpy + the ring's lock-free round trip. No allocation, no lock.
 * Planar layout matches PluginHostBridge: channel c at ring.audioData() + c * maxBlockSamples().
 */
namespace aizana
{
    struct OobAudioMarshal
    {
        /** HOST audio thread. Marshal `buffer` through `ring` to the child and back.
            Returns the ring status; on anything but ok the buffer is cleared (silence). */
        static SharedAudioRing::Status hostProcessBlock (SharedAudioRing& ring,
                                                         juce::AudioBuffer<float>& buffer,
                                                         uint32_t timeoutMicros) noexcept;
    };
}
