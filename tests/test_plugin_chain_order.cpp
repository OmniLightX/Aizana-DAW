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

// ---------------------------------------------------------------------------
// Helper: insert a specific built-in TE plugin type at a raw chain index.
// Used to construct chains with internals (volume/level) at known positions.
// ---------------------------------------------------------------------------
static te::Plugin* insertTePlugin (te::Track& track, EditManager& editManager,
                                   const char* xmlType, int rawIndex)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto pluginPtr = edit->getPluginCache().createNewPlugin (xmlType, juce::PluginDescription{});
    if (pluginPtr == nullptr)
        return nullptr;

    track.pluginList.insertPlugin (pluginPtr, rawIndex, nullptr);
    return pluginPtr.get();
}

// ---------------------------------------------------------------------------
// M3 regression (live-test 2026-07-02): the user-visible slot list hides
// internal plugins (volume/level), but those sit at the HEAD of a default
// track chain — the old `prefix + slotIndex` mapping therefore inserted new
// plugins BETWEEN existing user plugins (Fresh Air landed between the two
// compressors instead of after the de-esser).
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: chainInsertIndexForUserSlot maps visible slots to raw chain indices",
           "[plugins][PLUG-04][M3]")
{
    EditManager editManager;
    editManager.createNewEdit();
    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    PluginScanner scanner;
    PluginManager pm (scanner, editManager);

    // Build the exact live-test chain shape: [volume, level, EQ, comp, comp]
    track.pluginList.clear();
    insertTePlugin (track, editManager, te::VolumeAndPanPlugin::xmlTypeName, -1);
    insertTePlugin (track, editManager, te::LevelMeterPlugin::xmlTypeName,   -1);
    insertTePlugin (track, editManager, te::EqualiserPlugin::xmlTypeName,    -1);   // slot 0 → raw 2
    insertTePlugin (track, editManager, te::CompressorPlugin::xmlTypeName,   -1);   // slot 1 → raw 3
    insertTePlugin (track, editManager, te::CompressorPlugin::xmlTypeName,   -1);   // slot 2 → raw 4
    REQUIRE (track.pluginList.size() == 5);

    // Occupied slots map to the raw index of the plugin at that visible position
    CHECK (pm.chainInsertIndexForUserSlot (track, 0) == 2);
    CHECK (pm.chainInsertIndexForUserSlot (track, 1) == 3);
    CHECK (pm.chainInsertIndexForUserSlot (track, 2) == 4);

    // The trailing "+" slot appends AFTER the last user plugin — the old code
    // returned 3 here (prefixCount 0 + slotIndex 3) = between the compressors.
    CHECK (pm.chainInsertIndexForUserSlot (track, 3) == 5);

    // End-to-end: inserting via that index lands after the last compressor
    juce::PluginDescription desc;
    desc.pluginFormatName = "VST3";
    desc.name             = "Fresh Air (stand-in)";
    desc.fileOrIdentifier = "/nonexistent/freshair.vst3";
    desc.uniqueId         = 0x0F00BAA2;
    auto* inserted = pm.insertPlugin (track, desc, pm.chainInsertIndexForUserSlot (track, 3));
    REQUIRE (inserted != nullptr);
    CHECK (track.pluginList[5] == inserted);   // raw end — after both compressors
}

TEST_CASE ("PluginChain: chainInsertIndexForUserSlot with empty user chain keeps aux prefix",
           "[plugins][PLUG-04][M3]")
{
    EditManager editManager;
    editManager.createNewEdit();
    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    PluginScanner scanner;
    PluginManager pm (scanner, editManager);

    // FX-channel shape: [auxreturn, volume, level] — slot 0 must land after the auxreturn
    track.pluginList.clear();
    insertTePlugin (track, editManager, te::AuxReturnPlugin::xmlTypeName,    -1);
    insertTePlugin (track, editManager, te::VolumeAndPanPlugin::xmlTypeName, -1);
    insertTePlugin (track, editManager, te::LevelMeterPlugin::xmlTypeName,   -1);

    CHECK (pm.chainInsertIndexForUserSlot (track, 0) == 1);
}

// ---------------------------------------------------------------------------
// M4: drag-reorder between USER-VISIBLE slots must translate correctly to raw
// chain moves even with internals at the head of the chain.
// ---------------------------------------------------------------------------
TEST_CASE ("PluginChain: moveUserPluginToSlot reorders user plugins around internals (undoable)",
           "[plugins][PLUG-04][M4]")
{
    EditManager editManager;
    editManager.createNewEdit();
    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto& track = *tracks[0];

    PluginScanner scanner;
    PluginManager pm (scanner, editManager);

    // [volume, level, EQ, comp, deesser] — visible slots: EQ(0), comp(1), deesser(2)
    track.pluginList.clear();
    insertTePlugin (track, editManager, te::VolumeAndPanPlugin::xmlTypeName, -1);
    insertTePlugin (track, editManager, te::LevelMeterPlugin::xmlTypeName,   -1);
    auto* eq   = insertTePlugin (track, editManager, te::EqualiserPlugin::xmlTypeName,  -1);
    auto* comp = insertTePlugin (track, editManager, te::CompressorPlugin::xmlTypeName, -1);
    auto* dees = insertTePlugin (track, editManager, te::CompressorPlugin::xmlTypeName, -1);
    REQUIRE (eq != nullptr);
    REQUIRE (comp != nullptr);
    REQUIRE (dees != nullptr);

    // "De-esser to the top, before the EQ" — the exact move from the live test
    pm.moveUserPluginToSlot (track, 2, 0);
    CHECK (track.pluginList[2] == dees);
    CHECK (track.pluginList[3] == eq);
    CHECK (track.pluginList[4] == comp);
    CHECK (track.pluginList[0]->getPluginType() == juce::String ("volume"));   // internals untouched

    // One undo restores the original order
    editManager.undoLastAction();
    CHECK (track.pluginList[2] == eq);
    CHECK (track.pluginList[3] == comp);
    CHECK (track.pluginList[4] == dees);

    // Move down: EQ (slot 0) onto the trailing "+" region (slot 3 → clamps to end)
    pm.moveUserPluginToSlot (track, 0, 3);
    CHECK (track.pluginList[2] == comp);
    CHECK (track.pluginList[3] == dees);
    CHECK (track.pluginList[4] == eq);
}

// ---------------------------------------------------------------------------
// Regression: a third-party plugin must be registered with the ENGINE's knownPluginList
// when inserted. TE's ExternalPlugin::findMatchingPlugin() resolves the real instance
// from engine.getPluginManager().knownPluginList — NOT from Aizana' scanner list. Before
// this fix, inserted VST3/AU plugins loaded as shells: getAudioPluginInstance() was null,
// so the editor showed "No editor available" and no audio was processed.
TEST_CASE ("PluginManager: insertPlugin registers the description with the engine", "[plugins][PLUG-04]")
{
    EditManager editManager;
    editManager.createNewEdit();
    auto* edit = editManager.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (! tracks.isEmpty());
    auto* track = tracks[0];

    PluginScanner scanner;
    PluginManager pluginManager (scanner, editManager);

    juce::PluginDescription desc;
    desc.pluginFormatName  = "VST3";
    desc.name              = "Third-Party Test EQ";
    desc.manufacturerName  = "ACME";
    desc.fileOrIdentifier  = "/some/path/ThirdPartyTestEQ.vst3";
    desc.uniqueId          = 0x5EED1234;
    desc.numInputChannels  = 2;
    desc.numOutputChannels = 2;

    auto& engineList = editManager.getEngine().getPluginManager().knownPluginList;
    const auto idString = desc.createIdentifierString();
    REQUIRE (engineList.getTypeForIdentifierString (idString) == nullptr);   // not known yet

    // The file doesn't exist, so the actual instance won't load — that's fine; we are
    // asserting the engine now KNOWS the plugin (so it COULD instantiate a real one).
    pluginManager.insertPlugin (*track, desc);

    CHECK (engineList.getTypeForIdentifierString (idString) != nullptr);
}
