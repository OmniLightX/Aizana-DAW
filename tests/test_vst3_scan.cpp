// test_vst3_scan.cpp — PLUG-01: VST3 plugin hosting (Windows + macOS)
// Status: STUB — implementation added in plan 01-05 (plugin hosting)
//
// Tests JUCE VST3PluginFormat with KnownPluginList scanner.
// Requires a known test VST3 plugin to be available at a test path.
// Plugin scanner must run in child process (crash isolation).
#include <catch2/catch_test_macros.hpp>

TEST_CASE("VST3Scan: scanner finds known test plugin in designated directory", "[plugins][PLUG-01][stub]") {
    SKIP("Stub — implement when PluginScanner is available (plan 01-05)");
}

TEST_CASE("VST3Scan: scanned plugin can be instantiated and processes audio without crash", "[plugins][PLUG-01][stub]") {
    SKIP("Stub — implement when PluginScanner is available (plan 01-05)");
}

TEST_CASE("VST3Scan: KnownPluginList persists scan results across application restart", "[plugins][PLUG-01][stub]") {
    SKIP("Stub — implement when PluginScanner is available (plan 01-05)");
}
