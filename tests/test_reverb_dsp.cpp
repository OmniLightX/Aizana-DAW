// test_reverb_dsp.cpp — FX-03: Reverb (Room, Hall, Plate modes)
// Status: STUB — implementation added in plan 01-06 (DSP effects)
//
// Tests the reverb DSP processor. Wet signal must be non-zero (reverb is active).
// Dry passthrough must be clean (100% dry = identical to input within float tolerance).
// Different room modes (Room, Hall, Plate) must produce distinct impulse responses.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Reverb: wet output signal is non-zero with 100% wet mix", "[reverb][FX-03][stub]") {
    SKIP("Stub — implement when ReverbProcessor is available (plan 01-06)");
}

TEST_CASE("Reverb: dry passthrough at 100% dry is identical to input (within float tolerance)", "[reverb][FX-03][stub]") {
    SKIP("Stub — implement when ReverbProcessor is available (plan 01-06)");
}

TEST_CASE("Reverb: Room, Hall, Plate modes produce distinct impulse responses", "[reverb][FX-03][stub]") {
    SKIP("Stub — implement when ReverbProcessor is available (plan 01-06)");
}
