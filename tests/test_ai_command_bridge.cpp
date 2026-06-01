// test_ai_command_bridge.cpp — Phase 2 core: AI→TE Command Queue Bridge
//
// Verifies the bridge's contract:
//   - enqueued commands run on apply and mutate the Edit
//   - a batch collapses into exactly ONE undo transaction
//   - separate batches are separate undo steps
//   - empty batches are ignored
//   - onBatchApplied fires once per batch
//   - enqueue is safe from a background thread (the real AI-worker path)
//
// The async (AsyncUpdater) trigger is a thin wrapper over applyPendingCommands(),
// which the tests call directly for determinism. All tests run headless.

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"

#include <thread>
#include <atomic>

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: an enqueued command runs and is dequeued on apply",
           "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    AICommandBridge bridge (em);

    bool ran = false;
    bridge.enqueue ("test", [&ran] (juce::UndoManager&) { ran = true; });

    CHECK (bridge.getPendingBatchCount() == 1);
    CHECK_FALSE (ran);                 // nothing runs until applied (message thread)

    bridge.applyPendingCommands();

    CHECK (ran);
    CHECK (bridge.getPendingBatchCount() == 0);
}

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: a batch collapses into a single undo transaction",
           "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);

    // A throwaway node on the edit state to mutate (added without undo).
    juce::ValueTree node ("AITEST");
    edit->state.appendChild (node, nullptr);

    bridge.enqueueBatch ("set three props", {
        [node] (juce::UndoManager& um) mutable { node.setProperty ("a", 1, &um); },
        [node] (juce::UndoManager& um) mutable { node.setProperty ("b", 2, &um); },
        [node] (juce::UndoManager& um) mutable { node.setProperty ("c", 3, &um); },
    });
    bridge.applyPendingCommands();

    REQUIRE ((int) node.getProperty ("a") == 1);
    REQUIRE ((int) node.getProperty ("b") == 2);
    REQUIRE ((int) node.getProperty ("c") == 3);

    // ONE undo must revert the WHOLE batch.
    edit->getUndoManager().undo();

    CHECK_FALSE (node.hasProperty ("a"));
    CHECK_FALSE (node.hasProperty ("b"));
    CHECK_FALSE (node.hasProperty ("c"));
}

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: separate batches are separate undo steps", "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);

    juce::ValueTree node ("AITEST");
    edit->state.appendChild (node, nullptr);

    bridge.enqueue ("first",  [node] (juce::UndoManager& um) mutable { node.setProperty ("a", 1, &um); });
    bridge.enqueue ("second", [node] (juce::UndoManager& um) mutable { node.setProperty ("b", 2, &um); });
    bridge.applyPendingCommands();

    REQUIRE (node.hasProperty ("a"));
    REQUIRE (node.hasProperty ("b"));

    edit->getUndoManager().undo();   // reverts only the second batch
    CHECK (node.hasProperty ("a"));
    CHECK_FALSE (node.hasProperty ("b"));

    edit->getUndoManager().undo();   // reverts the first batch
    CHECK_FALSE (node.hasProperty ("a"));
}

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: empty batches are ignored", "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    AICommandBridge bridge (em);

    bridge.enqueueBatch ("empty", {});
    CHECK (bridge.getPendingBatchCount() == 0);
}

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: onBatchApplied fires once per batch", "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    AICommandBridge bridge (em);

    int fired = 0;
    juce::String lastDesc;
    bridge.onBatchApplied = [&] (const juce::String& d) { ++fired; lastDesc = d; };

    bridge.enqueue ("one", [] (juce::UndoManager&) {});
    bridge.enqueue ("two", [] (juce::UndoManager&) {});
    bridge.applyPendingCommands();

    CHECK (fired == 2);
    CHECK (lastDesc == "two");
}

// ---------------------------------------------------------------------------
TEST_CASE ("AICommandBridge: enqueue is safe from a background thread", "[ai][phase2]")
{
    EditManager em;
    em.createNewEdit();
    AICommandBridge bridge (em);

    std::atomic<int> ran { 0 };

    // Mirror the real AI-worker path: a background thread enqueues batches.
    std::thread worker ([&bridge, &ran]
    {
        for (int i = 0; i < 20; ++i)
            bridge.enqueue ("bg", [&ran] (juce::UndoManager&) { ran.fetch_add (1); });
    });
    worker.join();

    CHECK (bridge.getPendingBatchCount() == 20);

    bridge.applyPendingCommands();   // message thread applies
    CHECK (ran.load() == 20);
    CHECK (bridge.getPendingBatchCount() == 0);
}
