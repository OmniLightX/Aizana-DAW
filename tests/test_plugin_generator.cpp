// test_plugin_generator.cpp — Phase 4 / Iteration B, B1: the generator backend
// (template registry + DE/EN text→template matcher). Pure, headless.
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "generator/PluginGenerator.h"

using cloudes::PluginGenerator;

TEST_CASE ("PluginGenerator: registry has the curated templates", "[generator]")
{
    const auto& t = PluginGenerator::templates();
    REQUIRE (t.size() >= 3);
    REQUIRE (PluginGenerator::byId ("gain")    != nullptr);
    REQUIRE (PluginGenerator::byId ("lowpass") != nullptr);
    REQUIRE (PluginGenerator::byId ("drive")   != nullptr);
    REQUIRE (PluginGenerator::byId ("nope")    == nullptr);
    REQUIRE (PluginGenerator::byId ("gain")->clapFileName() == "gain.clap");
}

TEST_CASE ("PluginGenerator: text→template matcher (DE/EN)", "[generator]")
{
    auto idOf = [] (const juce::String& prompt) -> juce::String
    {
        const auto* m = PluginGenerator::match (prompt);
        return m != nullptr ? m->id : juce::String ("none");
    };

    REQUIRE (idOf ("mach es lauter")            == "gain");
    REQUIRE (idOf ("make it louder please")     == "gain");
    REQUIRE (idOf ("add some distortion")       == "drive");
    REQUIRE (idOf ("ich will mehr verzerrung")  == "drive");
    REQUIRE (idOf ("klingt zu dumpf, weniger höhen") == "lowpass");
    REQUIRE (idOf ("low-pass filter")           == "lowpass");

    REQUIRE (idOf ("")                          == "none");   // empty → no match
    REQUIRE (idOf ("völliger unsinn ohne bezug")== "none");   // nothing matches → nullptr
}
