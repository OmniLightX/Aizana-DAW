// test_saturator_dsp.cpp — P2 (S1-Parität): Built-in Saturator (Tape/Tube/Soft)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/SaturatorProcessor.h>
#include <cmath>

namespace
{
    juce::AudioBuffer<float> makeSineBuffer (float amp, int numSamples, double sr, double hz = 220.0)
    {
        juce::AudioBuffer<float> buf (2, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        return buf;
    }

    float peakLevel (const juce::AudioBuffer<float>& buf)
    {
        return buf.getMagnitude (0, 0, buf.getNumSamples());
    }

    float rmsLevel (const juce::AudioBuffer<float>& buf)
    {
        return buf.getRMSLevel (0, 0, buf.getNumSamples());
    }
}

TEST_CASE ("Saturator: drive compresses the crest factor (peaks saturate, RMS holds)", "[saturator][P2]")
{
    SaturatorProcessor sat;
    sat.setDrive (24.0f);
    sat.setTypeIndex (0);    // Tape
    sat.setTone (0.0f);
    sat.setMix (100.0f);
    sat.setOutputGain (0.0f);

    const double sr = 44100.0;
    const int n = 8192;
    sat.prepareToPlay (sr, n);

    auto buf = makeSineBuffer (0.8f, n, sr);
    const float peakIn = peakLevel (buf);
    const float rmsIn  = rmsLevel (buf);

    juce::MidiBuffer midi;
    sat.processBlock (buf, midi);

    const float crestIn  = peakIn / rmsIn;                    // sine ≈ 1.414
    const float crestOut = peakLevel (buf) / rmsLevel (buf);

    // Saturation flattens the tops → crest factor drops toward a square wave
    CHECK (crestOut < crestIn * 0.95f);
    // tanh is bounded — output can never clip past ±1
    CHECK (peakLevel (buf) <= 1.0f);
}

TEST_CASE ("Saturator: small signals stay near unity gain (auto makeup)", "[saturator][P2]")
{
    SaturatorProcessor sat;
    sat.setDrive (24.0f);
    sat.setTypeIndex (0);
    sat.setTone (0.0f);
    sat.setMix (100.0f);
    sat.setOutputGain (0.0f);

    const double sr = 44100.0;
    const int n = 8192;
    sat.prepareToPlay (sr, n);

    // Tiny signal: tanh(d·x)/d ≈ x → output ≈ input
    auto buf = makeSineBuffer (0.001f, n, sr);
    const float rmsIn = rmsLevel (buf);

    juce::MidiBuffer midi;
    sat.processBlock (buf, midi);

    CHECK_THAT (rmsLevel (buf), Catch::Matchers::WithinRel (rmsIn, 0.15f));
}

TEST_CASE ("Saturator: mix 0 % is a clean bypass", "[saturator][P2]")
{
    SaturatorProcessor sat;
    sat.setDrive (36.0f);
    sat.setTypeIndex (2);    // Soft Clip
    sat.setTone (50.0f);
    sat.setMix (0.0f);
    sat.setOutputGain (0.0f);

    const double sr = 44100.0;
    const int n = 4096;
    sat.prepareToPlay (sr, n);

    auto buf = makeSineBuffer (0.8f, n, sr);
    auto reference = buf;

    juce::MidiBuffer midi;
    sat.processBlock (buf, midi);

    for (int i = 0; i < n; i += 512)
        CHECK_THAT (buf.getSample (0, i),
                    Catch::Matchers::WithinAbs (reference.getSample (0, i), 1e-5f));
}

TEST_CASE ("Saturator: tube stage leaves no DC offset", "[saturator][P2]")
{
    SaturatorProcessor sat;
    sat.setDrive (24.0f);
    sat.setTypeIndex (1);    // Tube (asymmetric — needs the DC blocker)
    sat.setTone (0.0f);
    sat.setMix (100.0f);
    sat.setOutputGain (0.0f);

    const double sr = 44100.0;
    const int n = 44100;
    sat.prepareToPlay (sr, n);

    auto buf = makeSineBuffer (0.8f, n, sr);
    juce::MidiBuffer midi;
    sat.processBlock (buf, midi);

    // Mean over the last half (settled DC blocker) must be ~0
    double sum = 0.0;
    for (int i = n / 2; i < n; ++i)
        sum += buf.getSample (0, i);
    const double mean = sum / (n / 2);

    CHECK (std::abs (mean) < 0.01);
}
