// test_gate_dsp.cpp — P2 (S1-Parität): Built-in Gate (Threshold/Hold/Range + Sidechain-Key)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/GateProcessor.h>
#include <cmath>

namespace
{
    juce::AudioBuffer<float> makeConstantBuffer (int channels, float amp, int numSamples)
    {
        juce::AudioBuffer<float> buf (channels, numSamples);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                buf.setSample (ch, i, amp);
        return buf;
    }

    // 4-channel buffer: main = ch 0/1, key = ch 2/3
    juce::AudioBuffer<float> makeKeyedBuffer (float mainAmp, float scAmp, int numSamples)
    {
        juce::AudioBuffer<float> buf (4, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            buf.setSample (0, i, mainAmp);
            buf.setSample (1, i, mainAmp);
            buf.setSample (2, i, scAmp);
            buf.setSample (3, i, scAmp);
        }
        return buf;
    }
}

TEST_CASE ("Gate: signal below threshold is attenuated by range", "[gate][P2]")
{
    GateProcessor gate;
    gate.setThreshold (-30.0f);
    gate.setAttack (0.1f);
    gate.setHold (0.0f);
    gate.setRelease (5.0f);
    gate.setRange (60.0f);

    gate.prepareToPlay (44100.0, 8192);

    // -50 dB input, 20 dB below threshold → gate stays closed
    const float amp = std::pow (10.0f, -50.0f / 20.0f);
    auto buf = makeConstantBuffer (2, amp, 8192);
    juce::MidiBuffer midi;
    gate.processBlock (buf, midi);

    // Output settled far below the input level (range = 60 dB attenuation)
    CHECK (std::abs (buf.getSample (0, 8191)) < amp * 0.05f);
    CHECK (gate.getGainReductionDB() > 20.0f);
}

TEST_CASE ("Gate: signal above threshold passes at unity", "[gate][P2]")
{
    GateProcessor gate;
    gate.setThreshold (-30.0f);
    gate.setAttack (0.1f);
    gate.setHold (50.0f);
    gate.setRelease (100.0f);
    gate.setRange (80.0f);

    gate.prepareToPlay (44100.0, 8192);

    const float amp = std::pow (10.0f, -10.0f / 20.0f);   // 20 dB over threshold
    auto buf = makeConstantBuffer (2, amp, 8192);
    juce::MidiBuffer midi;
    gate.processBlock (buf, midi);

    CHECK_THAT (buf.getSample (0, 8191), Catch::Matchers::WithinAbs (amp, amp * 0.02f));
}

TEST_CASE ("Gate: hold keeps the gate open after the signal drops", "[gate][P2]")
{
    GateProcessor gate;
    gate.setThreshold (-30.0f);
    gate.setAttack (0.1f);
    gate.setHold (200.0f);     // 200 ms hold = 8820 samples
    gate.setRelease (5.0f);
    gate.setRange (80.0f);

    gate.prepareToPlay (44100.0, 4096);

    // Open the gate
    const float hot = std::pow (10.0f, -10.0f / 20.0f);
    {
        auto buf = makeConstantBuffer (2, hot, 4096);
        juce::MidiBuffer midi;
        gate.processBlock (buf, midi);
    }

    // 50 ms of silence — inside the hold window the gate must stay open
    const float tiny = std::pow (10.0f, -70.0f / 20.0f);
    auto buf = makeConstantBuffer (2, tiny, 2205);
    juce::MidiBuffer midi;
    gate.processBlock (buf, midi);

    CHECK_THAT (buf.getSample (0, 2204), Catch::Matchers::WithinAbs (tiny, tiny * 0.1f));
}

TEST_CASE ("Gate: external key opens the gate for a quiet main signal", "[gate][sidechain][P2]")
{
    GateProcessor gate;
    gate.setThreshold (-30.0f);
    gate.setAttack (0.1f);
    gate.setHold (50.0f);
    gate.setRelease (100.0f);
    gate.setRange (80.0f);
    gate.setSidechainKeyEnabled (true);
    REQUIRE (gate.isSidechainKeyEnabled());

    gate.prepareToPlay (44100.0, 8192);

    // Main quiet (-50 dB, below threshold), key hot (-10 dB) → gate opens anyway
    const float mainAmp = std::pow (10.0f, -50.0f / 20.0f);
    const float keyAmp  = std::pow (10.0f, -10.0f / 20.0f);
    auto buf = makeKeyedBuffer (mainAmp, keyAmp, 8192);
    juce::MidiBuffer midi;
    gate.processBlock (buf, midi);

    CHECK_THAT (buf.getSample (0, 8191), Catch::Matchers::WithinAbs (mainAmp, mainAmp * 0.05f));

    // Key channels pass through untouched (input-only)
    CHECK_THAT (buf.getSample (2, 8191), Catch::Matchers::WithinAbs (keyAmp, 1e-6f));
}

TEST_CASE ("Gate: silent external key closes the gate on a hot main signal", "[gate][sidechain][P2]")
{
    GateProcessor gate;
    gate.setThreshold (-30.0f);
    gate.setAttack (0.1f);
    gate.setHold (0.0f);
    gate.setRelease (5.0f);
    gate.setRange (80.0f);
    gate.setSidechainKeyEnabled (true);

    gate.prepareToPlay (44100.0, 8192);

    const float mainAmp = std::pow (10.0f, -10.0f / 20.0f);
    auto buf = makeKeyedBuffer (mainAmp, 0.0f, 8192);
    juce::MidiBuffer midi;
    gate.processBlock (buf, midi);

    CHECK (std::abs (buf.getSample (0, 8191)) < mainAmp * 0.05f);
}
