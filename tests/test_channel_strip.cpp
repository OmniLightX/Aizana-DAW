// test_channel_strip.cpp — MIX-01: Channel strip parameters (volume, pan, solo, mute)
// Status: STUB — implementation added in plan 01-04 (mixer)
//
// Tests Tracktion Engine Track with VolumeAndPanPlugin:
// - Volume parameter range and default (0 dB)
// - Pan parameter range (-1.0 to 1.0) and center default
// - Solo/mute flags persist across ValueTree serialisation round-trip
#include <catch2/catch_test_macros.hpp>

TEST_CASE("ChannelStrip: volume parameter defaults to 0 dB and spans -inf to +6 dB", "[mixer][MIX-01][stub]") {
    SKIP("Stub — implement when MixerEngine is available (plan 01-04)");
}

TEST_CASE("ChannelStrip: pan parameter defaults to center (0.0) and spans -1.0 to 1.0", "[mixer][MIX-01][stub]") {
    SKIP("Stub — implement when MixerEngine is available (plan 01-04)");
}

TEST_CASE("ChannelStrip: solo/mute flags survive ValueTree XML round-trip", "[mixer][MIX-01][stub]") {
    SKIP("Stub — implement when MixerEngine is available (plan 01-04)");
}
