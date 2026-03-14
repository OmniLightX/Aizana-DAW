// test_undo.cpp — AUDIO-05: Unlimited undo/redo including AI action grouping
// Status: STUB — implementation added in plan 01-02 (engine core)
//
// Tests the shared juce::UndoManager that routes all ValueTree mutations.
// AI-aware grouping: multiple track mutations from one AI action must collapse
// into a single undo step via UndoManager::beginNewTransaction().
#include <catch2/catch_test_macros.hpp>

TEST_CASE("UndoManager: 10 sequential edits are all individually undoable", "[undo][AUDIO-05][stub]") {
    SKIP("Stub — implement when EditManager is available (plan 01-02)");
}

TEST_CASE("UndoManager: AI batch transaction collapses to one undo step", "[undo][AUDIO-05][stub]") {
    SKIP("Stub — implement when EditManager is available (plan 01-02)");
}

TEST_CASE("UndoManager: redo restores state after undo", "[undo][AUDIO-05][stub]") {
    SKIP("Stub — implement when EditManager is available (plan 01-02)");
}
