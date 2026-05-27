// test_vst3_scan.cpp — PLUG-01: VST3 plugin hosting (Windows + macOS)
//
// Tests JUCE VST3PluginFormat with KnownPluginList scanner.
// Plugin scanner runs in a child process for crash isolation.
//
// NOTE: These tests require at least one VST3 plugin to be installed in the
// standard search paths. If no VST3 plugins are found, tests skip gracefully.

#include <catch2/catch_test_macros.hpp>
#include "../src/plugins/PluginScanner.h"

// ---------------------------------------------------------------------------
// Helper: build a temp directory to store the KnownPluginList XML
// ---------------------------------------------------------------------------
static juce::File getTempPluginListFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("cloudes_test_plugin_list.xml");
}

// ---------------------------------------------------------------------------
// VST3 Test 1: PluginScanner::scanForPlugins finds at least 1 VST3 entry
// ---------------------------------------------------------------------------
TEST_CASE ("VST3Scan: scanner finds at least one VST3 plugin in standard search paths",
           "[plugins][PLUG-01]")
{
    PluginScanner scanner;

    bool scanComplete = false;
    scanner.scanForPlugins (
        [](float) {},           // onProgress — ignore
        [&]() { scanComplete = true; }
    );
    scanner.waitForScanComplete (30000);

    REQUIRE (scanComplete);

    const auto& known = scanner.getKnownPlugins();

    // Look for any VST3 entry
    bool foundVST3 = false;
    for (int i = 0; i < known.getNumTypes(); ++i)
    {
        if (known.getType (i)->pluginFormatName == "VST3")
        {
            foundVST3 = true;
            break;
        }
    }

    if (! foundVST3)
        SKIP ("No VST3 plugins found in standard search paths — skipping VST3 scan test");

    REQUIRE (foundVST3);
}

// ---------------------------------------------------------------------------
// VST3 Test 2: KnownPluginList round-trips (savePluginList / loadPluginList)
// ---------------------------------------------------------------------------
TEST_CASE ("VST3Scan: KnownPluginList persists scan results across application restart",
           "[plugins][PLUG-01]")
{
    PluginScanner scanner;

    bool scanComplete = false;
    scanner.scanForPlugins (
        [](float) {},
        [&]() { scanComplete = true; }
    );
    scanner.waitForScanComplete (30000);
    REQUIRE (scanComplete);

    const int originalCount = scanner.getKnownPlugins().getNumTypes();

    if (originalCount == 0)
        SKIP ("No plugins found — skipping persistence round-trip test");

    // Save
    const auto listFile = getTempPluginListFile();
    scanner.savePluginList (listFile);
    REQUIRE (listFile.existsAsFile());

    // Load into a fresh scanner
    PluginScanner scanner2;
    scanner2.loadPluginList (listFile);

    REQUIRE (scanner2.getKnownPlugins().getNumTypes() == originalCount);

    // Clean up
    listFile.deleteFile();
}
