// test_reverb_dsp.cpp — FX-03: Reverb (Room, Hall, Plate modes)
// Tests the reverb DSP processor. Wet signal must be non-zero (reverb is active).
// Dry passthrough must be clean (100% dry = identical to input within float tolerance).
// Different room modes (Room, Hall, Plate) must produce distinct impulse responses.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/ReverbProcessor.h>
#include <vector>
#include <cmath>
#include <numeric>

TEST_CASE("Reverb: wet output is non-zero after impulse (100% wet)", "[reverb][FX-03]")
{
    ReverbProcessor reverb;
    reverb.setMode(ReverbMode::Room);
    reverb.setWetDry(1.0f);  // 100% wet
    reverb.setRoomSize(0.5f);
    reverb.setDamping(0.5f);

    const double sampleRate = 44100.0;
    const int numSamples = 8192;
    reverb.prepareToPlay(sampleRate, numSamples);

    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    buf.setSample(0, 0, 1.0f);  // Impulse at sample 0, channel 0
    buf.setSample(1, 0, 1.0f);  // Stereo

    juce::MidiBuffer midi;
    reverb.processBlock(buf, midi);

    // With 100% wet, reverb tail must have energy beyond the initial sample
    const float* ch0 = buf.getReadPointer(0);
    float energy = 0.0f;
    for (int n = 1; n < numSamples; ++n)  // Skip sample 0 (impulse itself)
        energy += ch0[n] * ch0[n];

    // Non-zero reverb tail
    REQUIRE(energy > 0.001f);

    // More than 100 samples of non-zero output
    int nonZeroCount = 0;
    for (int n = 1; n < numSamples; ++n)
        if (std::abs(ch0[n]) > 1e-6f) nonZeroCount++;
    REQUIRE(nonZeroCount > 100);
}

TEST_CASE("Reverb: dry passthrough at 100% dry is identical to input", "[reverb][FX-03]")
{
    ReverbProcessor reverb;
    reverb.setMode(ReverbMode::Room);
    reverb.setWetDry(0.0f);  // 100% dry

    const double sampleRate = 44100.0;
    const int numSamples = 512;
    reverb.prepareToPlay(sampleRate, numSamples);

    // Fill with a ramp signal
    juce::AudioBuffer<float> buf(2, numSamples);
    for (int n = 0; n < numSamples; ++n)
    {
        float val = (float)n / numSamples;
        buf.setSample(0, n, val);
        buf.setSample(1, n, val);
    }

    // Store original
    std::vector<float> original(numSamples);
    for (int n = 0; n < numSamples; ++n)
        original[n] = buf.getSample(0, n);

    juce::MidiBuffer midi;
    reverb.processBlock(buf, midi);

    // At 0% wet, output must equal input exactly (no reverb coloring)
    const float* ch0 = buf.getReadPointer(0);
    for (int n = 0; n < numSamples; ++n)
        REQUIRE_THAT(ch0[n], Catch::Matchers::WithinAbs(original[n], 1e-5f));
}

TEST_CASE("Reverb: Room and Hall modes produce different T60 decay times", "[reverb][FX-03]")
{
    const double sampleRate = 44100.0;
    const int numSamples = 44100;  // 1 second

    // Helper lambda to measure energy tail length
    auto measureTailEnergy = [&](ReverbMode mode) -> float {
        ReverbProcessor reverb;
        reverb.setMode(mode);
        reverb.setWetDry(1.0f);
        reverb.setRoomSize(0.7f);
        reverb.setDamping(0.3f);
        reverb.prepareToPlay(sampleRate, numSamples);

        juce::AudioBuffer<float> buf(2, numSamples);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);

        juce::MidiBuffer midi;
        reverb.processBlock(buf, midi);

        // Sum energy in the second half of the buffer (longer tail = more energy there)
        const float* ch0 = buf.getReadPointer(0);
        float energy = 0.0f;
        int halfPoint = numSamples / 2;
        for (int n = halfPoint; n < numSamples; ++n)
            energy += ch0[n] * ch0[n];
        return energy;
    };

    float roomEnergy = measureTailEnergy(ReverbMode::Room);
    float hallEnergy = measureTailEnergy(ReverbMode::Hall);

    // Hall mode should have longer decay (more energy in second half)
    REQUIRE(hallEnergy > roomEnergy);
}

TEST_CASE("Reverb: Plate mode has different character than Hall", "[reverb][FX-03]")
{
    const double sampleRate = 44100.0;
    const int numSamples = 22050;  // 0.5 second

    auto getImpulseResponse = [&](ReverbMode mode) -> std::vector<float> {
        ReverbProcessor reverb;
        reverb.setMode(mode);
        reverb.setWetDry(1.0f);
        reverb.setRoomSize(0.5f);
        reverb.setDamping(0.5f);
        reverb.prepareToPlay(sampleRate, numSamples);

        juce::AudioBuffer<float> buf(2, numSamples);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);

        juce::MidiBuffer midi;
        reverb.processBlock(buf, midi);

        std::vector<float> ir(numSamples);
        const float* ch0 = buf.getReadPointer(0);
        for (int n = 0; n < numSamples; ++n) ir[n] = ch0[n];
        return ir;
    };

    auto hallIR = getImpulseResponse(ReverbMode::Hall);
    auto plateIR = getImpulseResponse(ReverbMode::Plate);

    // Verify they produce different output (non-trivial difference)
    float sumDiff = 0.0f;
    for (int n = 0; n < numSamples; ++n)
        sumDiff += std::abs(hallIR[n] - plateIR[n]);

    REQUIRE(sumDiff > 0.01f);  // Hall and Plate must have distinct impulse responses
}

TEST_CASE("Reverb: prepareToPlay at two different sample rates does not crash", "[reverb][FX-03]")
{
    ReverbProcessor reverb;
    reverb.setMode(ReverbMode::Room);
    reverb.setWetDry(0.5f);

    REQUIRE_NOTHROW(reverb.prepareToPlay(44100.0, 512));

    // Run some audio through it
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    buf.setSample(0, 0, 1.0f);
    buf.setSample(1, 0, 1.0f);
    juce::MidiBuffer midi;
    reverb.processBlock(buf, midi);

    // Re-prepare at different sample rate — delay lines must resize correctly
    REQUIRE_NOTHROW(reverb.prepareToPlay(96000.0, 512));
}

TEST_CASE("Reverb: wet/dry mix is correctly applied (no gain artifact)", "[reverb][FX-03]")
{
    const double sampleRate = 44100.0;
    const int numSamples = 4096;

    // Process same impulse at 100% wet and 100% dry separately
    auto processAt = [&](float wet) -> std::vector<float> {
        ReverbProcessor reverb;
        reverb.setMode(ReverbMode::Room);
        reverb.setWetDry(wet);
        reverb.setRoomSize(0.5f);
        reverb.prepareToPlay(sampleRate, numSamples);

        juce::AudioBuffer<float> buf(2, numSamples);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        reverb.processBlock(buf, midi);

        std::vector<float> out(numSamples);
        const float* ch0 = buf.getReadPointer(0);
        for (int n = 0; n < numSamples; ++n) out[n] = ch0[n];
        return out;
    };

    // At 0.5 wet, output should be between all-dry and all-wet
    auto dryOutput  = processAt(0.0f);
    auto wetOutput  = processAt(1.0f);
    auto halfOutput = processAt(0.5f);

    // halfOutput should be a blend — verify it's not identical to either extreme
    float diffFromDry = 0.0f, diffFromWet = 0.0f;
    for (int n = 0; n < numSamples; ++n)
    {
        diffFromDry += std::abs(halfOutput[n] - dryOutput[n]);
        diffFromWet += std::abs(halfOutput[n] - wetOutput[n]);
    }

    REQUIRE(diffFromDry > 0.01f);
    REQUIRE(diffFromWet > 0.01f);
}
