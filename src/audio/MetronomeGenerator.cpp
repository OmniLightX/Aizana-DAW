#include "MetronomeGenerator.h"

void MetronomeGenerator::processBlock (float* const* outputData,
                                       int numChannels,
                                       int numSamples) noexcept
{
    if (! isEnabled.load() || outputData == nullptr || numChannels < 1)
        return;

    const double bpm    = currentBpm.load();
    const int    bpb    = beatsPerBar.load();
    const bool   accent = accentEnabled.load();
    const float  lvl    = level.load();

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

            // Accent the downbeat (beat 0): higher pitch AND louder than off-beats —
            // but only when the accent is enabled; otherwise every beat sounds the same.
            // Lock both in BEFORE incrementing beatInBar so this beat uses its own
            // values; they persist for the whole click burst. Scale by the level.
            const bool accentedDownbeat = accent && (beatInBar == 0);
            clickFreq = accentedDownbeat ? 1200.0 : 800.0;
            clickAmp  = (accentedDownbeat ? 0.34 : 0.20) * (double) lvl;

            // Advance beat-in-bar counter after accent selection
            beatInBar = (beatInBar + 1) % bpb;
        }

        // Synthesize click if active
        if (clickSamplesLeft > 0)
        {
            // Use clickFreq locked in at beat boundary — stays constant for the full click burst
            const double freq = clickFreq;

            // Amplitude: peak locked at beat boundary (accented downbeat = louder),
            // then quadratic decay over the click duration.
            const float envelope = (float)clickSamplesLeft / (float)clickDurationSamples;
            const float amplitude = (float)clickAmp * envelope * envelope;

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
