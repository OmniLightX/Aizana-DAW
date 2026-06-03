// test_ai_engine.cpp — Phase 2, Block B: AIEngine + inference scaffolding
//
// Verifies the worker-pool plumbing and the stub backend:
//   - submitBatch runs on a background thread and its batch reaches the bridge,
//     which applies it on the (test) message thread as one undo transaction
//   - many concurrent submissions are safe (the bridge queue is lock-guarded)
//   - the OnnxModelRunner stub is honest: not available, passthrough run()

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"
#include "ai/AIEngine.h"
#include "ai/OnnxModelRunner.h"

#include <atomic>

// ---------------------------------------------------------------------------
TEST_CASE ("AIEngine: a background batch reaches the bridge and applies as one undo", "[ai][phase2][engine]")
{
    EditManager em; em.createNewEdit();
    auto* edit = em.getEdit(); REQUIRE (edit != nullptr);
    AICommandBridge bridge (em);
    AIEngine engine (bridge);

    juce::ValueTree node ("AITEST");
    edit->state.appendChild (node, nullptr);

    engine.submitBatch ("bg batch", [node]() mutable
    {
        std::vector<AICommandBridge::Command> cmds;
        cmds.push_back ([node] (juce::UndoManager& um) mutable { node.setProperty ("x", 7, &um); });
        cmds.push_back ([node] (juce::UndoManager& um) mutable { node.setProperty ("y", 8, &um); });
        return cmds;
    });

    engine.waitUntilIdle (2000);     // wait for the worker to enqueue
    bridge.applyPendingCommands();   // message-thread apply (deterministic in test)

    REQUIRE ((int) node.getProperty ("x") == 7);
    REQUIRE ((int) node.getProperty ("y") == 8);

    edit->getUndoManager().undo();   // ONE undo reverts the whole batch
    CHECK_FALSE (node.hasProperty ("x"));
    CHECK_FALSE (node.hasProperty ("y"));
}

// ---------------------------------------------------------------------------
TEST_CASE ("AIEngine: many concurrent submissions are safe", "[ai][phase2][engine]")
{
    EditManager em; em.createNewEdit();
    AICommandBridge bridge (em);
    AIEngine engine (bridge);

    std::atomic<int> produced { 0 };
    for (int i = 0; i < 32; ++i)
        engine.submitBatch ("n", [&produced]()
        {
            produced.fetch_add (1);
            std::vector<AICommandBridge::Command> cmds;
            cmds.push_back ([] (juce::UndoManager&) {});
            return cmds;
        });

    engine.waitUntilIdle (3000);
    CHECK (produced.load() == 32);
    CHECK (bridge.getPendingBatchCount() == 32);

    bridge.applyPendingCommands();
    CHECK (bridge.getPendingBatchCount() == 0);
    CHECK (engine.getNumThreads() > 0);
}

// ---------------------------------------------------------------------------
TEST_CASE ("AIEngine: OnnxModelRunner stub is honest (unavailable, passthrough)", "[ai][phase2][engine]")
{
    OnnxModelRunner runner;
    CHECK_FALSE (runner.isAvailable());
    CHECK (runner.getName().isNotEmpty());

    std::vector<float> in { 1.0f, 2.0f, 3.0f };
    auto out = runner.run (in);
    CHECK (out == in);               // defined identity passthrough — no fabricated AI
}
