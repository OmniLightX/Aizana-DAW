// test_limiter_dsp.cpp — FX-04: Limiter (master bus, 0 dBFS ceiling)
// Tests the limiter DSP processor. Output must not exceed 0 dBFS under
// 6 dB of over-threshold signal. True-peak compliance is preferred.
// Verify the limiter does not colour or distort signals below the ceiling.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/LimiterProcessor.h>
#include <cmath>
#include <vector>

static juce::AudioBuffer<float> makeSineBuffer(float amplitudeLinear, float freqHz,
                                               double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    for (int n = 0; n < numSamples; ++n)
    {
        float val = amplitudeLinear * std::sin(2.0f * static_cast<float>(M_PI) * freqHz * n / (float)sampleRate);
        buf.setSample(0, n, val);
        buf.setSample(1, n, val);
    }
    return buf;
}

TEST_CASE("Limiter: all output samples <= 0 dBFS with 6dB headroom violation", "[limiter][FX-04]")
{
    LimiterProcessor limiter;
    limiter.setThreshold(0.0f);  // 0 dBFS ceiling
    limiter.setRelease(100.0f);

    const double sampleRate = 44100.0;
    const int numSamples = 44100;  // 1 second
    limiter.prepareToPlay(sampleRate, numSamples);

    // Input at +6 dBFS: amplitude = 2.0 (linear)
    auto buf = makeSineBuffer(2.0f, 440.0f, sampleRate, numSamples);

    juce::MidiBuffer midi;
    limiter.processBlock(buf, midi);

    // ALL output samples must be within [-1.0, +1.0] (0 dBFS)
    const float* ch0 = buf.getReadPointer(0);
    for (int n = 0; n < numSamples; ++n)
        REQUIRE(std::abs(ch0[n]) <= 1.0f + 1e-5f);
}

TEST_CASE("Limiter: signal below threshold passes through unmodified", "[limiter][FX-04]")
{
    LimiterProcessor limiter;
    limiter.setThreshold(0.0f);
    limiter.setRelease(100.0f);

    const double sampleRate = 44100.0;
    const int numSamples = 4096;
    limiter.prepareToPlay(sampleRate, numSamples);

    // Input at -6 dBFS: amplitude = 0.5 (well below 0 dBFS threshold)
    float amp = std::pow(10.0f, -6.0f / 20.0f);
    auto buf = makeSineBuffer(amp, 440.0f, sampleRate, numSamples);

    // Store original
    std::vector<float> original(numSamples);
    for (int n = 0; n < numSamples; ++n)
        original[n] = buf.getSample(0, n);

    juce::MidiBuffer midi;
    limiter.processBlock(buf, midi);

    // Output should be unmodified (or very close — juce::dsp::Limiter may have
    // tiny lookahead differences, so allow a generous tolerance here)
    const float* out = buf.getReadPointer(0);
    float maxDiff = 0.0f;
    for (int n = 0; n < numSamples; ++n)
        maxDiff = std::max(maxDiff, std::abs(out[n] - original[n]));

    // Should not be significantly altered (within 0.5dB)
    REQUIRE(maxDiff < amp * 0.06f);  // < 0.5dB relative error
}

TEST_CASE("Limiter: getGainReductionDB returns ~6dB when 6dB limiting is occurring", "[limiter][FX-04]")
{
    LimiterProcessor limiter;
    limiter.setThreshold(0.0f);
    limiter.setRelease(500.0f);  // Long release so GR is sustained

    const double sampleRate = 44100.0;
    const int blockSize = 4096;
    limiter.prepareToPlay(sampleRate, blockSize);

    // Drive limiter with +6dBFS signal for several blocks to reach steady state
    for (int i = 0; i < 5; ++i)
    {
        auto buf = makeSineBuffer(2.0f, 440.0f, sampleRate, blockSize);
        juce::MidiBuffer midi;
        limiter.processBlock(buf, midi);
    }

    float gr = limiter.getGainReductionDB();
    // Should show approximately 6dB of gain reduction
    REQUIRE_THAT(gr, Catch::Matchers::WithinAbs(6.0f, 2.0f));  // ±2dB tolerance (limiter responds dynamically)
}

TEST_CASE("Limiter: gain returns to 0dB after silence following limiting", "[limiter][FX-04]")
{
    LimiterProcessor limiter;
    limiter.setThreshold(0.0f);
    limiter.setRelease(100.0f);  // 100ms release

    const double sampleRate = 44100.0;
    const int blockSize = 512;
    limiter.prepareToPlay(sampleRate, blockSize);

    // Drive limiter to engaged state
    for (int i = 0; i < 10; ++i)
    {
        auto buf = makeSineBuffer(2.0f, 440.0f, sampleRate, blockSize);
        juce::MidiBuffer midi;
        limiter.processBlock(buf, midi);
    }

    // Now feed silence for >200ms (>= 200ms / (512/44100)ms = ~17 blocks)
    for (int i = 0; i < 20; ++i)
    {
        juce::AudioBuffer<float> silenceBuf(2, blockSize);
        silenceBuf.clear();
        juce::MidiBuffer midi;
        limiter.processBlock(silenceBuf, midi);
    }

    float gr = limiter.getGainReductionDB();
    // After release time, gain reduction should return near 0
    REQUIRE(gr < 1.0f);
}
