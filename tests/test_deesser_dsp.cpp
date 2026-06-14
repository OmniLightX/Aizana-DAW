// test_deesser_dsp.cpp — slim FabFilter Pro-DS-style de-esser.
// Verifies the split-band behaviour: sibilance-band energy above threshold is
// reduced (capped at Range), low-frequency content is left alone, Range caps
// the reduction, and bypass is transparent.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/DeEsserProcessor.h>
#include <cmath>
#include <vector>

namespace
{
    juce::AudioBuffer<float> makeSine (float freqHz, float amp, int numSamples, double sr)
    {
        juce::AudioBuffer<float> buf (1, numSamples);
        const double w = juce::MathConstants<double>::twoPi * freqHz / sr;
        for (int i = 0; i < numSamples; ++i)
            buf.setSample (0, i, amp * (float) std::sin (w * i));
        return buf;
    }

    float rms (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        const auto* p = b.getReadPointer (0);
        for (int i = 0; i < b.getNumSamples(); ++i) sum += (double) p[i] * p[i];
        return (float) std::sqrt (sum / juce::jmax (1, b.getNumSamples()));
    }

    // Run a steady tone through the de-esser for several blocks so the envelope
    // and crossover settle, returning the final processed block.
    juce::AudioBuffer<float> settle (DeEsserProcessor& ds, float freqHz, float amp,
                                     double sr, int block = 4096, int blocks = 6)
    {
        ds.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf;
        for (int i = 0; i < blocks; ++i)
        {
            buf = makeSine (freqHz, amp, block, sr);
            juce::MidiBuffer midi;
            ds.processBlock (buf, midi);
        }
        return buf;
    }
}

TEST_CASE ("De-Esser: sibilance band above threshold is reduced", "[deesser]")
{
    DeEsserProcessor ds;
    ds.setFrequency (6500.0f);
    ds.setThreshold (-24.0f);
    ds.setRange (12.0f);
    ds.setMode (DeEsserProcessor::Mode::Split);

    // 10 kHz tone at -6 dBFS: well inside the sibilance band, 18 dB over threshold.
    settle (ds, 10000.0f, 0.5f, 44100.0);

    const float gr = ds.getGainReductionDB();
    REQUIRE (gr > 6.0f);                 // it is de-essing
    REQUIRE (gr <= 12.0f + 0.01f);       // never beyond Range
}

TEST_CASE ("De-Esser: low-frequency content is left alone", "[deesser]")
{
    DeEsserProcessor ds;
    ds.setFrequency (6500.0f);
    ds.setThreshold (-24.0f);
    ds.setRange (12.0f);
    ds.setMode (DeEsserProcessor::Mode::Split);

    // 200 Hz tone at the same hot level: below the crossover → not sibilance.
    const auto in  = makeSine (200.0f, 0.5f, 4096, 44100.0);
    const auto out = settle (ds, 200.0f, 0.5f, 44100.0);

    REQUIRE (ds.getGainReductionDB() < 1.0f);            // no de-essing triggered
    // Split mode reconstructs the low band at full level (LR sum is magnitude-flat).
    REQUIRE_THAT (rms (out), Catch::Matchers::WithinRel (rms (in), 0.1f));
}

TEST_CASE ("De-Esser: Range caps the maximum reduction", "[deesser]")
{
    DeEsserProcessor ds;
    ds.setFrequency (6500.0f);
    ds.setThreshold (-40.0f);   // far below the signal → huge overshoot
    ds.setRange (6.0f);         // but reduction is capped here
    ds.setMode (DeEsserProcessor::Mode::Split);

    settle (ds, 10000.0f, 0.5f, 44100.0);

    REQUIRE_THAT (ds.getGainReductionDB(), Catch::Matchers::WithinAbs (6.0f, 1.0f));
}

TEST_CASE ("De-Esser: side-chain meter tracks the high band", "[deesser]")
{
    DeEsserProcessor ds;
    ds.setFrequency (6500.0f);
    ds.setThreshold (0.0f);     // never trigger — just observe the side-chain level
    ds.setRange (12.0f);

    settle (ds, 10000.0f, 0.5f, 44100.0);   // -6 dBFS tone in the band

    // Side-chain should report roughly the -6 dBFS tone level.
    REQUIRE_THAT (ds.getSidechainLevelDB(), Catch::Matchers::WithinAbs (-6.0f, 3.0f));
}

TEST_CASE ("De-Esser: bypass passes signal unmodified", "[deesser]")
{
    DeEsserProcessor ds;
    ds.setFrequency (6500.0f);
    ds.setThreshold (-40.0f);
    ds.setRange (12.0f);
    ds.setBypass (true);

    const double sr = 44100.0;
    const int block = 512;
    ds.prepareToPlay (sr, block);

    auto buf = makeSine (10000.0f, 0.5f, block, sr);
    std::vector<float> original (block);
    for (int i = 0; i < block; ++i) original[i] = buf.getSample (0, i);

    juce::MidiBuffer midi;
    ds.processBlock (buf, midi);

    const float* out = buf.getReadPointer (0);
    for (int i = 0; i < block; ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (original[i], 1e-6f));
}
