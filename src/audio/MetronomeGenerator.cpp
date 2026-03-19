#include "MetronomeGenerator.h"

void MetronomeGenerator::processBlock (float* const* outputData,
                                       int numChannels,
                                       int numSamples) noexcept
{
    if (! isEnabled.load() || outputData == nullptr || numChannels < 1)
        return;

    const double bpm = currentBpm.load();
    const int    bpb = beatsPerBar.load();

    // Samples per beat at current BPM and sample rate
    const double samplesPerBeat = sr * 60.0 / bpm;

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance phase accumulator
        phaseAccumulator += 1.0 / samplesPerBeat;

        // Beat boundary: phase wraps to >= 1.0
        if (phaseAccumulator >= 1.0)
        {
            phaseAccumulator -= 1.0;

            // Start a new click burst
            clickSamplesLeft = clickDurationSamples;
            clickPhase = 0.0;

            // Select frequency BEFORE incrementing beatInBar so beat 0 (downbeat)
            // correctly uses 1200 Hz — beatInBar still holds the value for this beat
            clickFreq = (beatInBar == 0) ? 1200.0 : 800.0;

            // Advance beat-in-bar counter after frequency selection
            beatInBar = (beatInBar + 1) % bpb;
        }

        // Synthesize click if active
        if (clickSamplesLeft > 0)
        {
            // Use clickFreq locked in at beat boundary — stays constant for the full click burst
            const double freq = clickFreq;

            // Amplitude: decays over the click duration
            const float envelope = (float)clickSamplesLeft / (float)clickDurationSamples;
            const float amplitude = 0.25f * envelope * envelope;  // quadratic decay — ~12 dBFS headroom, reference click level

            const double twoPi = 6.283185307179586;
            const float sample = amplitude * std::sin (clickPhase);
            clickPhase += twoPi * freq / sr;
            if (clickPhase > twoPi) clickPhase -= twoPi;

            // Add to all output channels
            for (int ch = 0; ch < numChannels; ++ch)
                if (outputData[ch] != nullptr)
                    outputData[ch][i] += sample;

            --clickSamplesLeft;
        }
    }
}
