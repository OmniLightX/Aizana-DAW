// test_au_scan.cpp — PLUG-02: AU plugin hosting (macOS only)
// Status: STUB — implementation added in plan 01-05 (plugin hosting)
//
// Tests JUCE AudioUnitPluginFormat. AU scanning is macOS-only and gated
// at compile time. The test must gracefully report SKIPPED on non-macOS.
// At least one system AU (e.g., AUPitch) must be detectable.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("AUScan: scanner finds a built-in macOS AudioUnit plugin (e.g., AUPitch)", "[plugins][PLUG-02][stub]") {
    SKIP("Stub — implement when PluginScanner AU support is available (plan 01-05)");
}

TEST_CASE("AUScan: AU plugin instantiates without error on macOS", "[plugins][PLUG-02][stub]") {
    SKIP("Stub — implement when PluginScanner AU support is available (plan 01-05)");
}

TEST_CASE("AUScan: non-macOS build reports SKIPPED (AU not available on this platform)", "[plugins][PLUG-02][stub]") {
    SKIP("Stub — implement when PluginScanner AU support is available (plan 01-05)");
}
