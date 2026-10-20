// test_vst3_scan.cpp — PLUG-01: VST3 plugin hosting (Windows + macOS)
//
// Tests JUCE VST3PluginFormat with KnownPluginList scanner.
// Plugin scanner runs in a child process for crash isolation.
//
// NOTE: These tests require at least one VST3 plugin to be installed in the
// standard search paths. If no VST3 plugins are found, tests skip gracefully.

#include <catch2/catch_test_macros.hpp>
#include "../src/plugins/PluginScanner.h"
#include "SelectedTestPlugins.h"

// ---------------------------------------------------------------------------
// Helper: build a temp directory to store the KnownPluginList XML
// ---------------------------------------------------------------------------
static juce::File getTempPluginListFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("aizana_test_plugin_list.xml");
}

// ---------------------------------------------------------------------------
// VST3 Test 1: PluginScanner::scanForPlugins finds at least 1 VST3 entry
// ---------------------------------------------------------------------------
TEST_CASE ("VST3Scan: scanner finds the explicitly selected VST3 plugin",
           "[plugins][PLUG-01]")
{
    PluginScanner scanner;
    REQUIRE(scanner.getFormatManager().getNumFormats() >= (JUCE_MAC ? 4 : 3));

    bool scanComplete = false;
    scanTestPlugins (scanner, scanComplete);

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
        SKIP ("Set AIZANA_TEST_VST3 to run the external VST3 integration test");

    REQUIRE (foundVST3);
}

// ---------------------------------------------------------------------------
// VST3 Test 2: KnownPluginList round-trips (savePluginList / loadPluginList)
// ---------------------------------------------------------------------------
TEST_CASE ("VST3Scan: KnownPluginList persists scan results across application restart",
           "[plugins][PLUG-01]")
{
    PluginScanner scanner;
    REQUIRE(scanner.getFormatManager().getNumFormats() >= (JUCE_MAC ? 4 : 3));

    bool scanComplete = false;
    scanTestPlugins (scanner, scanComplete);
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

// ---------------------------------------------------------------------------
// VST3 Test 3: the "scan complete" resume marker round-trips through the cache.
// A partial cache (markComplete=false) must report lastScanWasComplete()==false
// so the app knows to RESUME scanning on the next launch; a complete cache must
// report true. Uses the 4 pre-loaded built-in FX — no real scan required, so
// this is fully deterministic and fast.
// ---------------------------------------------------------------------------
TEST_CASE ("VST3Scan: scan-complete marker round-trips (drives auto-resume)",
           "[plugins][PLUG-01]")
{
    const auto listFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("aizana_test_scan_marker.xml");
    listFile.deleteFile();

    // A fresh scanner defaults to "complete" (an empty/manual list needs no resume).
    PluginScanner fresh;
    REQUIRE (fresh.lastScanWasComplete());

    // Partial save → load → must be flagged incomplete (resume needed).
    {
        PluginScanner writer;
        writer.savePluginList (listFile, /*markComplete=*/false);
        REQUIRE (listFile.existsAsFile());

        PluginScanner reader;
        reader.loadPluginList (listFile);
        REQUIRE_FALSE (reader.lastScanWasComplete());
        // Built-ins survive the round-trip.
        REQUIRE (reader.getKnownPlugins().getNumTypes() >= 1);
    }

    // Complete save → load → must be flagged complete (no resume).
    {
        PluginScanner writer;
        writer.savePluginList (listFile, /*markComplete=*/true);

        PluginScanner reader;
        reader.loadPluginList (listFile);
        REQUIRE (reader.lastScanWasComplete());
    }

    listFile.deleteFile();
}
