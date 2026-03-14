// test_eq_dsp.cpp — FX-01: Parametric EQ (minimum 4 bands)
// Status: STUB — implementation added in plan 01-06 (DSP effects)
//
// Tests the EQ DSP engine using juce::dsp::IIR::Filter per band.
// Magnitude response at key frequencies must match expected dB values
// within tolerance (±0.5 dB at test frequency).
#include <catch2/catch_test_macros.hpp>

TEST_CASE("EQ: low shelf at 100 Hz boosts magnitude by expected dB", "[eq][FX-01][stub]") {
    SKIP("Stub — implement when EqProcessor is available (plan 01-06)");
}

TEST_CASE("EQ: peak band at 1 kHz produces correct magnitude response at center frequency", "[eq][FX-01][stub]") {
    SKIP("Stub — implement when EqProcessor is available (plan 01-06)");
}

TEST_CASE("EQ: high shelf at 10 kHz cuts magnitude by expected dB", "[eq][FX-01][stub]") {
    SKIP("Stub — implement when EqProcessor is available (plan 01-06)");
}
