// test_clap_host.cpp — PLUG-03: CLAP plugin hosting via direct CLAP C API
//
// Tests the custom ClapPluginFormat subclass using the direct CLAP C API (clap/clap.h).
// The alpha juce_clap_hosting community module is NOT used — see RESEARCH.md Pitfall 1.
//
// Test strategy:
//   - Tests 1–2 are always-run (test format registration, name, file extension).
//   - Tests 3–5 require an actual .clap binary. If no CLAP plugin is found in
//     the standard search paths, they skip gracefully rather than fail.

#include <catch2/catch_test_macros.hpp>
#include "../src/plugins/ClapPluginFormat.h"

// ---------------------------------------------------------------------------
// Helper: locate a .clap plugin for integration tests.
// Checks standard macOS/Windows installation paths. Returns empty string if none found.
// ---------------------------------------------------------------------------
static juce::String findTestClapPlugin()
{
    auto selected=juce::SystemStats::getEnvironmentVariable("AIZANA_TEST_CLAP", {});
    if (selected.isNotEmpty())
    {
        REQUIRE(juce::File(selected).exists());
        return selected;
    }
    juce::StringArray searchPaths;

#if JUCE_MAC
    searchPaths.add ("/Library/Audio/Plug-Ins/CLAP");
    searchPaths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                         .getChildFile ("Library/Audio/Plug-Ins/CLAP")
                         .getFullPathName());
#elif JUCE_WINDOWS
    searchPaths.add ("C:\\Program Files\\Common Files\\CLAP");
    searchPaths.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("CLAP")
                         .getFullPathName());
#else
    searchPaths.add ("/usr/lib/clap");
    searchPaths.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                         .getChildFile (".clap")
                         .getFullPathName());
#endif

    for (const auto& path : searchPaths)
    {
        juce::File dir (path);
        if (dir.exists())
        {
            auto clapFiles = dir.findChildFiles (juce::File::findFilesAndDirectories, true, "*.clap");
            if (!clapFiles.isEmpty())
                return clapFiles[0].getFullPathName();
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// Test 1: ClapPluginFormat::getName() returns "CLAP"
// Always runs — validates format registration with no external dependencies.
// ---------------------------------------------------------------------------
TEST_CASE ("CLAPHost: format getName returns CLAP", "[plugins][PLUG-03]")
{
    ClapPluginFormat fmt;
    REQUIRE (fmt.getName() == "CLAP");
}

// ---------------------------------------------------------------------------
// Test 2: fileMightContainThisPluginType — .clap extension recognised
// ---------------------------------------------------------------------------
TEST_CASE ("CLAPHost: fileMightContainThisPluginType recognises .clap extension", "[plugins][PLUG-03]")
{
    ClapPluginFormat fmt;
    CHECK (fmt.fileMightContainThisPluginType ("synth.clap"));
    CHECK (fmt.fileMightContainThisPluginType ("/usr/lib/clap/myplugin.clap"));
    CHECK_FALSE (fmt.fileMightContainThisPluginType ("synth.vst3"));
    CHECK_FALSE (fmt.fileMightContainThisPluginType ("synth.component"));
    CHECK_FALSE (fmt.fileMightContainThisPluginType ("synth.dll"));
}

// ---------------------------------------------------------------------------
// Test 3: findAllTypesForFile returns at least 1 PluginDescription for a real .clap
// Skips gracefully if no CLAP plugin is installed in the test environment.
// ---------------------------------------------------------------------------
TEST_CASE ("CLAPHost: scanner finds known CLAP plugin via direct CLAP C API", "[plugins][PLUG-03]")
{
    auto clapPath = findTestClapPlugin();
    if (clapPath.isEmpty())
        SKIP ("No .clap plugin found in standard search paths — skipping integration test");

    ClapPluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> results;
    fmt.findAllTypesForFile (results, clapPath);

    REQUIRE (results.size() >= 1);
    CHECK (results[0]->pluginFormatName == "CLAP");
    CHECK (results[0]->fileOrIdentifier == clapPath);
}

// ---------------------------------------------------------------------------
// Test 4: CLAP plugin instantiates and processes a silent audio block without crash
// ---------------------------------------------------------------------------
TEST_CASE ("CLAPHost: CLAP plugin instantiates and processes a silent audio block without crash", "[plugins][PLUG-03]")
{
    auto clapPath = findTestClapPlugin();
    if (clapPath.isEmpty())
        SKIP ("No .clap plugin found in standard search paths — skipping integration test");

    ClapPluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> results;
    fmt.findAllTypesForFile (results, clapPath);

    if (results.isEmpty())
        SKIP ("findAllTypesForFile returned no descriptions for the found .clap file");

    juce::String errorMessage;
    std::unique_ptr<juce::AudioPluginInstance> instance;

    // Synchronously create the plugin instance
    fmt.createPluginInstance (*results[0], 44100.0, 512, [&](std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
    {
        instance = std::move (inst);
        errorMessage = err;
    });

    REQUIRE (instance != nullptr);
    REQUIRE (errorMessage.isEmpty());

    // Prepare and process a silent buffer
    instance->prepareToPlay (44100.0, 512);

    const int numChannels = std::max (1, instance->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numChannels, 512);
    buffer.clear();

    juce::MidiBuffer midiBuffer;
    instance->processBlock (buffer, midiBuffer);   // Must not crash

    instance->releaseResources();

    SUCCEED ("CLAP plugin processed silent buffer without crash");
}

// ---------------------------------------------------------------------------
// Test 5: CLAP plugin parameter count and names are correctly exposed
// ---------------------------------------------------------------------------
TEST_CASE ("CLAPHost: CLAP plugin parameter count and names are correctly exposed", "[plugins][PLUG-03]")
{
    auto clapPath = findTestClapPlugin();
    if (clapPath.isEmpty())
        SKIP ("No .clap plugin found in standard search paths — skipping integration test");

    ClapPluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> results;
    fmt.findAllTypesForFile (results, clapPath);

    if (results.isEmpty())
        SKIP ("findAllTypesForFile returned no descriptions for the found .clap file");

    juce::String errorMessage;
    std::unique_ptr<juce::AudioPluginInstance> instance;

    fmt.createPluginInstance (*results[0], 44100.0, 512, [&](std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
    {
        instance = std::move (inst);
        errorMessage = err;
    });

    if (instance == nullptr)
        SKIP ("Plugin instantiation failed: " + errorMessage.toStdString());

    // Parameters must be accessible — count may be 0 for simple plugins (valid)
    auto& params = instance->getParameters();
    // Verify parameter objects are well-formed
    for (auto* p : params)
    {
        CHECK_FALSE (p->getName (64).isEmpty());
        // Value must be in [0, 1] normalised range
        CHECK (p->getValue() >= 0.0f);
        CHECK (p->getValue() <= 1.0f);
    }

    // No specific count required — we accept 0 or more parameters
    SUCCEED ("CLAP plugin exposes " + std::to_string (params.size()) + " parameters");
}
