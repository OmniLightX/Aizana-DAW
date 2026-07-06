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

    SharedAudioRing::Status OobAudioMarshal::hostProcessBlock (SharedAudioRing& ring,
                                                               juce::AudioBuffer<float>& buffer,
                                                               juce::MidiBuffer& midi,
                                                               uint32_t timeoutMicros)
    {
        encodeMidi (ring.midiInRegion(), SharedAudioRing::kMidiRegionBytes, midi);

        const auto status = hostProcessBlock (ring, buffer, timeoutMicros);

        midi.clear();
        if (status == SharedAudioRing::Status::ok)
            decodeMidi (ring.midiOutRegion(), SharedAudioRing::kMidiRegionBytes, midi);

        return status;
    }

    void OobAudioMarshal::writeParamEvents (SharedAudioRing& ring,
                                            const OopParamEvent* events, uint32_t count) noexcept
    {
        uint8_t* region = ring.paramRegion();
        count = juce::jmin (count, SharedAudioRing::kMaxParamEvents);

        uint8_t* p = region + 4;
        for (uint32_t i = 0; i < count; ++i)
        {
            std::memcpy (p,     &events[i].index, 4);
            std::memcpy (p + 4, &events[i].value, 4);
            p += 8;
        }
        std::memcpy (region, &count, 4);   // count last: region is only read post-roundTrip anyway
    }

    void OobAudioMarshal::encodeMidi (uint8_t* region, uint32_t regionBytes,
                                      const juce::MidiBuffer& midi) noexcept
    {
        uint32_t count = 0;
        uint8_t* p   = region + 4;
        uint8_t* end = region + regionBytes;

        for (const auto metadata : midi)
        {
            const uint32_t sz      = (uint32_t) metadata.numBytes;
            const uint32_t padded  = (sz + 3u) & ~3u;

            if (p + 8 + padded > end)
                break;   // region full — drop the tail rather than corrupt the layout

            const int32_t pos = (int32_t) metadata.samplePosition;
            std::memcpy (p,     &pos, 4);
            std::memcpy (p + 4, &sz,  4);
            std::memcpy (p + 8, metadata.data, sz);
            p += 8 + padded;
            ++count;
        }

        std::memcpy (region, &count, 4);
    }

    void OobAudioMarshal::decodeMidi (const uint8_t* region, uint32_t regionBytes,
                                      juce::MidiBuffer& out)
    {
        uint32_t count = 0;
        std::memcpy (&count, region, 4);

        const uint8_t* p   = region + 4;
        const uint8_t* end = region + regionBytes;

        for (uint32_t i = 0; i < count; ++i)
        {
            if (p + 8 > end)
                break;

            int32_t  pos = 0;
            uint32_t sz  = 0;
            std::memcpy (&pos, p,     4);
            std::memcpy (&sz,  p + 4, 4);

            const uint32_t padded = (sz + 3u) & ~3u;
            if (sz == 0 || p + 8 + padded > end)
                break;   // malformed / truncated — stop decoding, never read past the region

            out.addEvent (reinterpret_cast<const void*> (p + 8), (int) sz, (int) pos);
            p += 8 + padded;
        }
    }
}
