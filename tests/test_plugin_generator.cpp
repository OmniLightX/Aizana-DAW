// test_plugin_generator.cpp — Phase 4 / Iteration B, B1: the generator backend
// (template registry + DE/EN text→template matcher). Pure, headless.
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "generator/PluginGenerator.h"

using aizana::PluginGenerator;

TEST_CASE ("PluginGenerator: registry has the real plugin templates", "[generator]")
{
    const auto& t = PluginGenerator::templates();
    REQUIRE (t.size() >= 5);
    REQUIRE (PluginGenerator::byId ("compressor") != nullptr);
    REQUIRE (PluginGenerator::byId ("eq3")        != nullptr);
    REQUIRE (PluginGenerator::byId ("reverb")     != nullptr);
    REQUIRE (PluginGenerator::byId ("delay")      != nullptr);
    REQUIRE (PluginGenerator::byId ("drive")      != nullptr);
    REQUIRE (PluginGenerator::byId ("gain")       == nullptr);   // toy removed
    REQUIRE (PluginGenerator::byId ("compressor")->clapFileName() == "compressor.clap");
}

TEST_CASE ("PluginGenerator: text→template matcher (DE/EN)", "[generator]")
{
    auto idOf = [] (const juce::String& prompt) -> juce::String
    {
        const auto* m = PluginGenerator::match (prompt);
        return m != nullptr ? m->id : juce::String ("none");
    };

    REQUIRE (idOf ("add some compression")        == "compressor");
    REQUIRE (idOf ("mehr punch, kompressor")      == "compressor");
    REQUIRE (idOf ("mehr bass im eq")             == "eq3");
    // Real UTF-8 (as a JUCE TextEditor delivers it) — a plain "ö" literal mojibakes
    // via juce::String(const char*) in this build, which is exactly why keywords use fromUTF8.
    REQUIRE (idOf (juce::String::fromUTF8 ("die höhen formen")) == "eq3");
    REQUIRE (idOf ("etwas hall / reverb")         == "reverb");
    REQUIRE (idOf ("ein delay echo")              == "delay");
    REQUIRE (idOf ("ich will mehr verzerrung")    == "drive");
    REQUIRE (idOf ("add some distortion")         == "drive");

    REQUIRE (idOf ("")                            == "none");   // empty → no match
    REQUIRE (idOf ("völliger unsinn ohne bezug")  == "none");   // nothing matches → nullptr
}
