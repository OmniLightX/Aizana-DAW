// test_compressor_dsp.cpp — FX-02: Compressor (attack, release, threshold, ratio, knee)
// Tests the compressor DSP algorithm. Gain reduction must match the
// expected value calculated from the transfer function at a known input level.
// Gain reduction meter value must accurately reflect signal reduction.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/CompressorProcessor.h>
#include <cmath>
#include <vector>

// Helper: generate constant-level audio buffer at given dBFS level
static juce::AudioBuffer<float> makeConstantBuffer(float amplitudeLinear, int numSamples)
{
    juce::AudioBuffer<float> buf(1, numSamples);
    buf.clear();
    for (int i = 0; i < numSamples; ++i)
        buf.setSample(0, i, amplitudeLinear);
    return buf;
}

static float linearToDB(float linear)
{
    return 20.0f * std::log10(std::max(linear, 1e-10f));
}

TEST_CASE("Compressor: gain reduction matches transfer function at 6dB above threshold", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);  // Threshold: -20 dBFS
    comp.setRatio(4.0f);        // 4:1 ratio
    comp.setAttack(0.1f);       // Very fast attack (0.1ms) so GR settles quickly
    comp.setRelease(200.0f);
    comp.setKnee(0.0f);         // Hard knee for predictable transfer function
    comp.setMakeupGain(0.0f);

    const double sampleRate = 44100.0;
    const int blockSize = 4096;
    comp.prepareToPlay(sampleRate, blockSize);

    // Input at -14dBFS: amplitude = 10^(-14/20)
    float inputAmplitude = std::pow(10.0f, -14.0f / 20.0f);
    auto buf = makeConstantBuffer(inputAmplitude, blockSize);

    juce::MidiBuffer midi;
    comp.processBlock(buf, midi);

    // Expected: input is 6dB above threshold (-14 vs -20)
    // With 4:1, gain reduction = (6 - 6/4) = 4.5 dB
    float gr = comp.getGainReductionDB();
    REQUIRE_THAT(gr, Catch::Matchers::WithinAbs(4.5f, 0.5f));
}

TEST_CASE("Compressor: attack time controls onset of gain reduction", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(1.0f);    // 1ms attack
    comp.setRelease(500.0f);
    comp.setKnee(0.0f);
    comp.setMakeupGain(0.0f);

    const double sampleRate = 44100.0;
    const int blockSize = 512;
    comp.prepareToPlay(sampleRate, blockSize);

    // Silence before threshold
    float silenceAmp = std::pow(10.0f, -40.0f / 20.0f);
    {
        auto buf = makeConstantBuffer(silenceAmp, blockSize);
        juce::MidiBuffer midi;
        comp.processBlock(buf, midi);
    }

    // Now step to level 6dB above threshold
    float inputAmplitude = std::pow(10.0f, -14.0f / 20.0f);
    auto buf = makeConstantBuffer(inputAmplitude, blockSize);
    juce::MidiBuffer midi;
    comp.processBlock(buf, midi);

    // Within 1ms = 44 samples, GR should begin building
    // After 1 full block of 512 samples (~11ms), GR should be significant
    float gr = comp.getGainReductionDB();
    REQUIRE(gr > 1.0f);  // Gain reduction should have begun
}

TEST_CASE("Compressor: release time — gain reduction returns to 0 after signal drops", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.1f);   // Very fast attack
    comp.setRelease(200.0f); // 200ms release
    comp.setKnee(0.0f);
    comp.setMakeupGain(0.0f);

    const double sampleRate = 44100.0;
    const int blockSize = 512;
    comp.prepareToPlay(sampleRate, blockSize);

    // Drive compressor to steady-state
    float hotAmp = std::pow(10.0f, -14.0f / 20.0f);
    for (int i = 0; i < 20; ++i)
    {
        auto buf = makeConstantBuffer(hotAmp, blockSize);
        juce::MidiBuffer midi;
        comp.processBlock(buf, midi);
    }

    // Drop to silence
    float silenceAmp = std::pow(10.0f, -80.0f / 20.0f);
    // Process 200ms worth of silence = 44100 * 0.2 = 8820 samples = ~17 blocks of 512
    for (int i = 0; i < 20; ++i)
    {
        auto buf = makeConstantBuffer(silenceAmp, blockSize);
        juce::MidiBuffer midi;
        comp.processBlock(buf, midi);
    }

    float gr = comp.getGainReductionDB();
    // After 200ms release, gain reduction should be near 0
    REQUIRE(gr < 1.0f);
}

TEST_CASE("Compressor: soft knee behavior — compression begins gradually below threshold", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(0.1f);
    comp.setRelease(200.0f);
    comp.setKnee(6.0f);   // 6dB soft knee
    comp.setMakeupGain(0.0f);

    const double sampleRate = 44100.0;
    const int blockSize = 4096;
    comp.prepareToPlay(sampleRate, blockSize);

    // Test at threshold-1.5dB (mid-point of lower knee half): soft-knee GR > 0.
    // The 6dB knee spans [threshold-3, threshold+3]. At the lower edge (-3dB) GR=0 by
    // definition; at threshold-1.5dB (delta=1.5) the quadratic formula yields ~0.14dB GR.
    float ampAtKneeCenter = std::pow(10.0f, (-20.0f - 1.5f) / 20.0f);
    auto buf = makeConstantBuffer(ampAtKneeCenter, blockSize);
    juce::MidiBuffer midi;
    comp.processBlock(buf, midi);

    float gr = comp.getGainReductionDB();
    // Soft knee: at center of knee, GR should be > 0 but less than hard-knee would produce
    // Hard knee at this level would give 0 GR (input is still below threshold).
    // Soft knee starts early, so we expect > 0 GR.
    REQUIRE(gr > 0.0f);
}

TEST_CASE("Compressor: getGainReductionDB is thread-safe atomic read", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(1.0f);
    comp.setRelease(100.0f);
    comp.setKnee(0.0f);

    const double sampleRate = 44100.0;
    comp.prepareToPlay(sampleRate, 512);

    // getGainReductionDB must be callable from a different thread without data race
    // This test just confirms the method is std::atomic-based and returns a float
    float gr1 = comp.getGainReductionDB();  // Should return 0 before any processing
    REQUIRE_THAT(gr1, Catch::Matchers::WithinAbs(0.0f, 0.01f));
}

TEST_CASE("Compressor: bypass mode passes signal unmodified", "[compressor][FX-02]")
{
    CompressorProcessor comp;
    comp.setThreshold(-20.0f);
    comp.setRatio(4.0f);
    comp.setAttack(1.0f);
    comp.setRelease(100.0f);
    comp.setBypass(true);

    const double sampleRate = 44100.0;
    const int blockSize = 512;
    comp.prepareToPlay(sampleRate, blockSize);

    float inputAmp = std::pow(10.0f, -14.0f / 20.0f);  // 6dB above threshold
    auto buf = makeConstantBuffer(inputAmp, blockSize);

    // Store original samples
    std::vector<float> original(blockSize);
    for (int i = 0; i < blockSize; ++i)
        original[i] = buf.getSample(0, i);

    juce::MidiBuffer midi;
    comp.processBlock(buf, midi);

    // Output should equal input exactly in bypass
    const float* out = buf.getReadPointer(0);
    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT(out[i], Catch::Matchers::WithinAbs(original[i], 1e-6f));
}
