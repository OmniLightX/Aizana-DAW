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
    /** One host→child parameter change (E5.4). value is the JUCE-normalised 0..1 float. */
    struct OopParamEvent
    {
        uint32_t index;
        float    value;
    };

    struct OobAudioMarshal
    {
        /** HOST audio thread. Marshal `buffer` through `ring` to the child and back.
            Returns the ring status; on anything but ok the buffer is cleared (silence). */
        static SharedAudioRing::Status hostProcessBlock (SharedAudioRing& ring,
                                                         juce::AudioBuffer<float>& buffer,
                                                         uint32_t timeoutMicros) noexcept;

        /** E5.4 HOST audio thread. Like above, but also carries MIDI both ways: `midi` is
            written into the ring's midi-in region before the round trip and replaced with the
            child's midi-out afterwards (cleared on a dead/hung child). */
        static SharedAudioRing::Status hostProcessBlock (SharedAudioRing& ring,
                                                         juce::AudioBuffer<float>& buffer,
                                                         juce::MidiBuffer& midi,
                                                         uint32_t timeoutMicros);

        /** E5.4 HOST audio thread. Publish pending parameter changes for the NEXT round trip.
            Call before hostProcessBlock; the ordering rides the ring's seq counters. */
        static void writeParamEvents (SharedAudioRing& ring,
                                      const OopParamEvent* events, uint32_t count) noexcept;

        // -- shared wire helpers (host + child use the same encoding) ---------------------
        /** Serialise `midi` into a control region: [count][{samplePos,size,bytes(4-aligned)}…]. */
        static void encodeMidi (uint8_t* region, uint32_t regionBytes,
                                const juce::MidiBuffer& midi) noexcept;

        /** Append every event in a control region to `out` (caller clears `out` first). */
        static void decodeMidi (const uint8_t* region, uint32_t regionBytes,
                                juce::MidiBuffer& out);
    };
}
