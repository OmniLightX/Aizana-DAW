#include "OobAudioMarshal.h"

#include <cstring>

namespace aizana
{
    SharedAudioRing::Status OobAudioMarshal::hostProcessBlock (SharedAudioRing& ring,
                                                              juce::AudioBuffer<float>& buffer,
                                                              uint32_t timeoutMicros) noexcept
    {
        const uint32_t stride = ring.maxBlockSamples();
        const int      n      = buffer.getNumSamples();
        const int      numCh  = juce::jmin (buffer.getNumChannels(), (int) ring.maxChannels());

        // Block bigger than the ring was sized for, or no channels → can't process safely.
        if (numCh <= 0 || (uint32_t) n > stride)
        {
            buffer.clear();
            return SharedAudioRing::Status::timedOut;
        }

        float* const base = ring.audioData();

        // Input → shared planar region (channel c at base + c*stride).
        for (int c = 0; c < numCh; ++c)
            std::memcpy (base + (size_t) c * (size_t) stride,
                         buffer.getReadPointer (c),
                         sizeof (float) * (size_t) n);

        const auto status = ring.roundTrip ((uint32_t) numCh, (uint32_t) n, timeoutMicros);

        if (status == SharedAudioRing::Status::ok)
        {
            // Processed output → back into the buffer.
            for (int c = 0; c < numCh; ++c)
                std::memcpy (buffer.getWritePointer (c),
                             base + (size_t) c * (size_t) stride,
                             sizeof (float) * (size_t) n);
        }
        else
        {
            // Hung or crashed child: silence. Never pass stale/garbage audio downstream.
            buffer.clear();
        }

        return status;
    }
}
