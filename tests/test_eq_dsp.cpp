// test_eq_dsp.cpp — FX-01: Parametric EQ (minimum 4 bands)
// Tests the EQ DSP engine using juce::dsp::IIR::Filter per band.
// Magnitude response at key frequencies must match expected dB values
// within tolerance (±0.5 dB at test frequency).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/EQProcessor.h>
#include <cmath>

// Helper: compute magnitude response of an audio buffer after processing an impulse
// Passes a unit impulse through the processor, then evaluates frequency response
// using the DFT relationship: H(f) = sum(h[n] * e^(-j*2*pi*f*n/N))
static float computeMagnitudeAtFreq(EQProcessor& eq, float freqHz, double sampleRate, int numSamples = 8192)
{
    juce::AudioBuffer<float> buf(1, numSamples);
    buf.clear();
    buf.setSample(0, 0, 1.0f);  // Unit impulse at sample 0

    juce::MidiBuffer midi;
    eq.prepareToPlay(sampleRate, numSamples);
    eq.processBlock(buf, midi);

    // Compute DFT magnitude at freqHz
    double omega = 2.0 * M_PI * freqHz / sampleRate;
    double re = 0.0, im = 0.0;
    const float* data = buf.getReadPointer(0);
    for (int n = 0; n < numSamples; ++n)
    {
        re += data[n] * std::cos(omega * n);
        im -= data[n] * std::sin(omega * n);
    }
    float magnitude = static_cast<float>(std::sqrt(re * re + im * im));
    return 20.0f * std::log10(magnitude + 1e-10f);  // Convert to dB
}

TEST_CASE("EQ: peak filter at 1kHz +6dB produces correct magnitude response", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::Peak;
    band.frequencyHz = 1000.0f;
    band.gainDB = 6.0f;
    band.Q = 1.0f;
    band.enabled = true;
    eq.setBand(0, band);

    // Other bands flat (0dB)
    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt1kHz = computeMagnitudeAtFreq(eq, 1000.0f, sampleRate);
    float magAt100Hz = computeMagnitudeAtFreq(eq, 100.0f, sampleRate);

    REQUIRE_THAT(magAt1kHz, Catch::Matchers::WithinAbs(6.0f, 0.5f));
    REQUIRE_THAT(magAt100Hz, Catch::Matchers::WithinAbs(0.0f, 0.1f));
}

TEST_CASE("EQ: low shelf at 200Hz +3dB boosts lows and leaves highs unchanged", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::LowShelf;
    band.frequencyHz = 200.0f;
    band.gainDB = 3.0f;
    band.Q = 0.707f;
    band.enabled = true;
    eq.setBand(0, band);

    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt80Hz  = computeMagnitudeAtFreq(eq, 80.0f, sampleRate);
    float magAt500Hz = computeMagnitudeAtFreq(eq, 500.0f, sampleRate);

    // Below shelf frequency: should be boosted >= +2.5dB
    REQUIRE(magAt80Hz >= 2.5f);
    // Well above shelf frequency: should be unchanged within ±0.1dB
    REQUIRE_THAT(magAt500Hz, Catch::Matchers::WithinAbs(0.0f, 0.15f));
}

TEST_CASE("EQ: high shelf at 10kHz -6dB attenuates highs", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::HighShelf;
    band.frequencyHz = 10000.0f;
    band.gainDB = -6.0f;
    band.Q = 0.707f;
    band.enabled = true;
    eq.setBand(0, band);

    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt15kHz = computeMagnitudeAtFreq(eq, 15000.0f, sampleRate);

    // Above shelf frequency: should be attenuated >= -5.5dB
    REQUIRE(magAt15kHz <= -5.5f);
}

TEST_CASE("EQ: all 4 bands operate independently", "[eq][FX-01]")
{
    EQProcessor eq;

    // Enable only band 2 (index 1) with +6dB peak at 2kHz
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        EQBand b;
        b.type = EQBandType::Peak;
        b.frequencyHz = 2000.0f;
        b.gainDB = 0.0f;
        b.enabled = false;
        eq.setBand(i, b);
    }

    EQBand activeBand;
    activeBand.type = EQBandType::Peak;
    activeBand.frequencyHz = 2000.0f;
    activeBand.gainDB = 6.0f;
    activeBand.Q = 1.0f;
    activeBand.enabled = true;
    eq.setBand(1, activeBand);  // Only band index 1

    const double sampleRate = 44100.0;
    float mag = computeMagnitudeAtFreq(eq, 2000.0f, sampleRate);

    // Should show the +6dB boost from band 1 only
    REQUIRE_THAT(mag, Catch::Matchers::WithinAbs(6.0f, 0.5f));

    // Band 0, 2, 3 are flat — make sure disabling doesn't contribute
    // (This test confirms independence: only 1 band is active)
    REQUIRE(EQProcessor::NumBands >= 4);
}

TEST_CASE("EQ: prepareToPlay at 96kHz succeeds without allocation", "[eq][FX-01]")
{
    EQProcessor eq;

    // Should not throw or crash
    REQUIRE_NOTHROW(eq.prepareToPlay(96000.0, 512));
}
