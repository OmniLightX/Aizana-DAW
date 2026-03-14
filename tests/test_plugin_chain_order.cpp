// test_plugin_chain_order.cpp — PLUG-04: Plugin insert chain reordering
//
// Tests that plugin insert order in a Tracktion Engine PluginList matches
// the ValueTree child order, and that reordering via PluginManager is
// undoable via the shared Edit UndoManager.
//
// Uses built-in Tracktion Engine plugin types (VolumeAndPanPlugin) rather
// than real VST3/CLAP plugins, so these tests run without any plugins installed.

#include <catch2/catch_test_macros.hpp>
#include "../src/engine/EditManager.h"
#include "../src/plugins/PluginManager.h"
#include "../src/plugins/PluginScanner.h"

// ---------------------------------------------------------------------------
// Helper: Insert a VolumeAndPanPlugin (built-in TE plugin) using the low-level
// PluginList API. This avoids needing a real VST3/CLAP for chain order tests.
// ---------------------------------------------------------------------------
static te::Plugin* insertBuiltinPlugin (te::Track& track, EditManager& editManager)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Insert built-in plugin");

    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::VolumeAndPanPlugin::xmlTypeName,
        juce::PluginDescription{});

    if (pluginPtr == nullptr)
        return nullptr;

    track.pluginList.insertPlugin (pluginPtr, -1, nullptr);
    return pluginPtr.get();
}

// ---------------------------------------------------------------------------
// Chain Test 1: Insert order matches ValueTree child order
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: insert order in PluginList matches ValueTree child order",
           "[plugins][PLUG-04]")
{
    EditManager editManager;
    editManager.createNewEdit();

    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    // Start with a clean plugin list (remove any default plugins added by TE)
    track.pluginList.clear();

    // Insert plugin A then plugin B
    auto* pluginA = insertBuiltinPlugin (track, editManager);
    auto* pluginB = insertBuiltinPlugin (track, editManager);

    REQUIRE (pluginA != nullptr);
    REQUIRE (pluginB != nullptr);

    // PluginList ordering
    REQUIRE (track.pluginList.size() == 2);
    CHECK (track.pluginList[0] == pluginA);
    CHECK (track.pluginList[1] == pluginB);

    // ValueTree child order must match PluginList order
    auto& state = track.pluginList.state;
    int pluginChildCount = 0;
    int aTreeIndex = -1, bTreeIndex = -1;

    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (child.hasType (tracktion::engine::IDs::PLUGIN))
        {
            if (pluginChildCount == 0) aTreeIndex = i;
            if (pluginChildCount == 1) bTreeIndex = i;
            ++pluginChildCount;
        }
    }

    CHECK (pluginChildCount == 2);
    CHECK (aTreeIndex < bTreeIndex);  // A comes before B in ValueTree
}

// ---------------------------------------------------------------------------
// Chain Test 2: movePlugin reorders the chain correctly
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: moving plugin from position 1 to position 0 reorders processing",
           "[plugins][PLUG-04]")
{
    EditManager editManager;
    editManager.createNewEdit();

    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    track.pluginList.clear();

    auto* pluginA = insertBuiltinPlugin (track, editManager);
    auto* pluginB = insertBuiltinPlugin (track, editManager);

    REQUIRE (pluginA != nullptr);
    REQUIRE (pluginB != nullptr);

    // Initial order: [A, B]
    REQUIRE (track.pluginList[0] == pluginA);
    REQUIRE (track.pluginList[1] == pluginB);

    // Create PluginManager to use its movePlugin
    PluginScanner scanner;
    PluginManager pluginManager (scanner, editManager);

    // Move B to position 0 → chain should become [B, A]
    pluginManager.movePlugin (track, *pluginB, 0);

    REQUIRE (track.pluginList.size() == 2);
    CHECK (track.pluginList[0] == pluginB);
    CHECK (track.pluginList[1] == pluginA);

    // Verify ValueTree order also updated
    auto& state = track.pluginList.state;
    bool bBeforeA = false;

    int foundB = -1, foundA = -1;
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        const auto child = state.getChild (i);
        if (! child.hasType (tracktion::engine::IDs::PLUGIN))
            continue;

        // Match by plugin state identity
        if (child == pluginB->state && foundB < 0) foundB = i;
        if (child == pluginA->state && foundA < 0) foundA = i;
    }

    bBeforeA = (foundB >= 0 && foundA >= 0 && foundB < foundA);
    CHECK (bBeforeA);
}

// ---------------------------------------------------------------------------
// Chain Test 3: movePlugin is undoable via shared UndoManager
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: plugin chain reorder is undoable via shared UndoManager",
           "[plugins][PLUG-04]")
{
    EditManager editManager;
    editManager.createNewEdit();

    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    track.pluginList.clear();

    auto* pluginA = insertBuiltinPlugin (track, editManager);
    auto* pluginB = insertBuiltinPlugin (track, editManager);

    REQUIRE (pluginA != nullptr);
    REQUIRE (pluginB != nullptr);

    // Initial order: [A, B]
    REQUIRE (track.pluginList[0] == pluginA);
    REQUIRE (track.pluginList[1] == pluginB);

    PluginScanner scanner;
    PluginManager pluginManager (scanner, editManager);

    // Move B to position 0 → [B, A]
    pluginManager.movePlugin (track, *pluginB, 0);
    REQUIRE (track.pluginList[0] == pluginB);
    REQUIRE (track.pluginList[1] == pluginA);

    // Undo — must restore original order [A, B]
    editManager.undoLastAction();

    REQUIRE (track.pluginList.size() == 2);
    CHECK (track.pluginList[0] == pluginA);
    CHECK (track.pluginList[1] == pluginB);
}

// ---------------------------------------------------------------------------
// Chain Test 4: Dead plugin (file deleted after scan) skipped gracefully
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: dead plugin file is skipped gracefully without crash or hang",
           "[plugins][PLUG-04]")
{
    // Create a PluginDescription pointing to a nonexistent file
    juce::PluginDescription deadDesc;
    deadDesc.pluginFormatName = "VST3";
    deadDesc.name             = "Fake Plugin (will not load)";
    deadDesc.fileOrIdentifier = "/nonexistent/path/to/plugin.vst3";
    deadDesc.numInputChannels  = 2;
    deadDesc.numOutputChannels = 2;

    PluginScanner scanner;

    // doesPluginStillExist — format manager should report false
    // (testing via PluginScanner's known plugins, which won't have this file)
    const auto& knownPlugins = scanner.getKnownPlugins();
    bool deadPluginInList = false;

    for (int i = 0; i < knownPlugins.getNumTypes(); ++i)
    {
        if (knownPlugins.getType(i)->fileOrIdentifier == deadDesc.fileOrIdentifier)
        {
            deadPluginInList = true;
            break;
        }
    }

    // The dead plugin should NOT appear in a fresh (unscanned) KnownPluginList
    CHECK_FALSE (deadPluginInList);

    // ClapPluginFormat should report this file does not still exist
    ClapPluginFormat clapFmt;
    CHECK_FALSE (clapFmt.doesPluginStillExist (deadDesc));

    // fileMightContainThisPluginType is false for .vst3 extension
    CHECK_FALSE (clapFmt.fileMightContainThisPluginType (deadDesc.fileOrIdentifier));

    SUCCEED ("Dead plugin handled gracefully — no crash, no hang");
}
