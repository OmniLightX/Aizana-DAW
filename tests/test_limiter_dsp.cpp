// test_limiter_dsp.cpp — FX-04: Limiter (master bus, 0 dBFS ceiling)
// Status: STUB — implementation added in plan 01-06 (DSP effects)
//
// Tests the limiter DSP processor. Output must not exceed 0 dBFS under
// 6 dB of over-threshold signal. True-peak compliance is preferred.
// Verify the limiter does not colour or distort signals below the ceiling.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Limiter: output does not exceed 0 dBFS with 6 dB of input headroom violation", "[limiter][FX-04][stub]") {
    SKIP("Stub — implement when LimiterProcessor is available (plan 01-06)");
}

TEST_CASE("Limiter: signal below ceiling passes through unmodified", "[limiter][FX-04][stub]") {
    SKIP("Stub — implement when LimiterProcessor is available (plan 01-06)");
}

TEST_CASE("Limiter: gain reduction is released smoothly (no clicks on release tail)", "[limiter][FX-04][stub]") {
    SKIP("Stub — implement when LimiterProcessor is available (plan 01-06)");
}
