// test_clap_host.cpp — PLUG-03: CLAP plugin hosting
// Status: STUB — implementation added in plan 01-05 (plugin hosting)
//
// Tests the custom CLAP AudioPluginFormat subclass using the direct CLAP C API.
// (juce_clap_hosting alpha module is NOT used — see RESEARCH.md Pitfall 1)
// A known CLAP test plugin must load, instantiate, and process audio correctly.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("CLAPHost: scanner finds known CLAP plugin via direct CLAP C API", "[plugins][PLUG-03][stub]") {
    SKIP("Stub — implement when ClapAudioPluginFormat is available (plan 01-05)");
}

TEST_CASE("CLAPHost: CLAP plugin instantiates and processes a silent audio block without crash", "[plugins][PLUG-03][stub]") {
    SKIP("Stub — implement when ClapAudioPluginFormat is available (plan 01-05)");
}

TEST_CASE("CLAPHost: CLAP plugin parameter count and names are correctly exposed", "[plugins][PLUG-03][stub]") {
    SKIP("Stub — implement when ClapAudioPluginFormat is available (plan 01-05)");
}
