// test_lufs_meter.cpp — EXP-03: Real-time LUFS metering (EBU R128)
// Status: STUB — implementation added in plan 01-07 (export + metering)
//
// Tests the LUFS metering pipeline using libebur128.
// Integrated LUFS measurement for a known test signal (1 kHz sine at -23 dBFS)
// must match the expected value within ±0.1 LU tolerance.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("LUFSMeter: integrated LUFS of -23 dBFS sine matches EBU R128 reference", "[metering][EXP-03][stub]") {
    SKIP("Stub — implement when LUFSMeter is available (plan 01-07)");
}

TEST_CASE("LUFSMeter: momentary LUFS updates at 400ms intervals per EBU R128 spec", "[metering][EXP-03][stub]") {
    SKIP("Stub — implement when LUFSMeter is available (plan 01-07)");
}

TEST_CASE("LUFSMeter: short-term LUFS uses 3-second integration window", "[metering][EXP-03][stub]") {
    SKIP("Stub — implement when LUFSMeter is available (plan 01-07)");
}
