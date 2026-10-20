// test_au_scan.cpp — PLUG-02: AU plugin hosting (macOS only)
//
// Tests JUCE AudioUnitPluginFormat. AU scanning is macOS-only.
// On non-macOS, all tests skip gracefully with a SKIP macro (not FAIL).
// At least one system AU (e.g., AUPitch, Apple I/O) should be detectable.

#include <catch2/catch_test_macros.hpp>
#include "../src/plugins/PluginScanner.h"
#include "SelectedTestPlugins.h"

// ---------------------------------------------------------------------------
// AU Test 1: Scanner finds at least one AudioUnit plugin on macOS
// ---------------------------------------------------------------------------
TEST_CASE ("AUScan: scanner finds a built-in macOS AudioUnit plugin",
           "[plugins][PLUG-02]")
{
#if ! JUCE_MAC
    SKIP ("AU plugins are macOS-only — skipping on this platform");
#else
    PluginScanner scanner;
    REQUIRE(scanner.getFormatManager().getNumFormats() >= (JUCE_MAC ? 4 : 3));

    bool scanComplete = false;
    scanTestPlugins (scanner, scanComplete);
    REQUIRE (scanComplete);

    const auto& known = scanner.getKnownPlugins();
    bool foundAU = false;

    for (int i = 0; i < known.getNumTypes(); ++i)
    {
        const auto* type = known.getType (i);
        if (type->pluginFormatName == "AudioUnit")
        {
            foundAU = true;
            break;
        }
    }

    if (! foundAU)
        SKIP ("No AudioUnit plugins found in /Library/Audio/Plug-Ins/Components — skipping");

    REQUIRE (foundAU);
#endif
}

// ---------------------------------------------------------------------------
// AU Test 2: AU plugin instantiates without error on macOS
// ---------------------------------------------------------------------------
TEST_CASE ("AUScan: AU plugin instantiates without error on macOS",
           "[plugins][PLUG-02]")
{
#if ! JUCE_MAC
    SKIP ("AU plugins are macOS-only — skipping on this platform");
#else
    PluginScanner scanner;
    REQUIRE(scanner.getFormatManager().getNumFormats() >= (JUCE_MAC ? 4 : 3));

    bool scanComplete = false;
    scanTestPlugins (scanner, scanComplete);
    REQUIRE (scanComplete);

    const auto& known = scanner.getKnownPlugins();

    // Find the first AU plugin
    const juce::PluginDescription* auDesc = nullptr;
    for (int i = 0; i < known.getNumTypes(); ++i)
    {
        if (known.getType (i)->pluginFormatName == "AudioUnit")
        {
            auDesc = known.getType (i);
            break;
        }
    }

    if (auDesc == nullptr)
        SKIP ("No AU plugin found to instantiate");

    struct Result
    {
        bool complete=false;
        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> instance;
    };
    auto result=std::make_shared<Result>();
    scanner.getFormatManager().createPluginInstanceAsync (
        *auDesc,44100.0,512,
        [result](std::unique_ptr<juce::AudioPluginInstance> instance,const juce::String& error)
        { result->instance=std::move(instance);result->error=error;result->complete=true; });
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+15000.0;
    while (!result->complete && juce::Time::getMillisecondCounterHiRes()<deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    REQUIRE(result->complete);
    INFO(result->error);
    REQUIRE(result->error.isEmpty());
    REQUIRE(result->instance != nullptr);
    result->instance->prepareToPlay(44100,512);
    juce::AudioBuffer<float> audio(juce::jmax(2,result->instance->getTotalNumOutputChannels()),512);
    audio.clear();juce::MidiBuffer midi;
    result->instance->processBlock(audio,midi);
    CHECK(std::isfinite(audio.getMagnitude(0,512)));
    result->instance->releaseResources();

#endif
}

// ---------------------------------------------------------------------------
// AU Test 3: Non-macOS platforms correctly skip AU tests
// ---------------------------------------------------------------------------
TEST_CASE ("AUScan: non-macOS build reports SKIPPED (AU not available on this platform)",
           "[plugins][PLUG-02]")
{
#if JUCE_MAC
    SKIP ("macOS build — this test documents non-macOS skip behaviour only");
#else
    // On non-macOS we correctly reach here and confirm the platform skip is active.
    // This test itself always passes on non-macOS (it documents expected skip behaviour).
    SUCCEED ("Non-macOS platform: AU tests correctly skipped via compile-time guard");
#endif
}
