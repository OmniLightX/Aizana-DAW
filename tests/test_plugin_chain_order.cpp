// test_plugin_chain_order.cpp — PLUG-04: Plugin insert chain reordering
// Status: STUB — implementation added in plan 01-05 (plugin hosting)
//
// Tests that plugin insert order in a Tracktion Engine PluginList matches
// the ValueTree child order. Reordering via ValueTree mutation must be
// reflected in the audio graph processing order (signal flows serially).
#include <catch2/catch_test_macros.hpp>

TEST_CASE("PluginChain: insert order in PluginList matches ValueTree child order", "[plugins][PLUG-04][stub]") {
    SKIP("Stub — implement when PluginChainManager is available (plan 01-05)");
}

TEST_CASE("PluginChain: moving plugin from position 1 to position 3 reorders processing", "[plugins][PLUG-04][stub]") {
    SKIP("Stub — implement when PluginChainManager is available (plan 01-05)");
}

TEST_CASE("PluginChain: plugin chain reorder is undoable via shared UndoManager", "[plugins][PLUG-04][stub]") {
    SKIP("Stub — implement when PluginChainManager is available (plan 01-05)");
}
