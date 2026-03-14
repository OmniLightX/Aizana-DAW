// test_sidechain_routing.cpp — MIX-03: Sidechain routing (kick -> compressor)
// Status: STUB — implementation added in plan 01-04 (mixer)
//
// Tests that sidechain bus routing correctly delivers the source track signal
// to the sidechain input of a plugin on the destination track.
// Uses Tracktion Engine FolderTrack + AuxSendPlugin / AuxReturnPlugin pattern.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Sidechain: source track audio reaches sidechain input of destination plugin", "[sidechain][MIX-03][stub]") {
    SKIP("Stub — implement when SidechainRouter is available (plan 01-04)");
}

TEST_CASE("Sidechain: sidechain signal level matches source track output level", "[sidechain][MIX-03][stub]") {
    SKIP("Stub — implement when SidechainRouter is available (plan 01-04)");
}

TEST_CASE("Sidechain: removing sidechain connection stops signal flow to plugin input", "[sidechain][MIX-03][stub]") {
    SKIP("Stub — implement when SidechainRouter is available (plan 01-04)");
}
