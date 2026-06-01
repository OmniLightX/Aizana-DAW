#pragma once

#include "EditManager.h"
#include <vector>
#include <functional>

/**
 * AICommandBridge — the AI→Tracktion-Engine command queue (Phase 2 core).
 *
 * Architecture invariant (CLAUDE.md): "AI→TE mutations ONLY via Command Queue
 * (AI thread enqueues, Message thread applies) — one UndoManager transaction per
 * AI batch action." This class IS that queue. AI inference runs on a dedicated
 * background thread and must never touch the Edit directly; instead it enqueues
 * batches of commands here, and the message thread drains and applies them.
 *
 *   AI background thread                Message thread
 *   ────────────────────                ──────────────
 *   enqueueBatch(desc, cmds)  ──push──▶  pending  ──AsyncUpdater──▶ applyPendingCommands()
 *   triggerAsyncUpdate()                                              beginAITransaction(desc)
 *                                                                     cmd(undoManager) … (ONE txn)
 *                                                                     onBatchApplied(desc)
 *
 * Threading model:
 *   - The queue is guarded by a juce::CriticalSection. This bridge sits between a
 *     BACKGROUND thread and the MESSAGE thread — NOT the audio thread — so a lock is
 *     correct and simplest. (The no-locks rule is an AUDIO-thread rule only.)
 *   - Commands run exclusively on the message thread and may freely allocate, lock,
 *     and mutate the Edit.
 *
 * Command contract:
 *   - A command mutates the Edit using the juce::UndoManager handed to it.
 *   - A command MUST NOT call beginNewTransaction() — the bridge owns the per-batch
 *     transaction so the whole batch is a single, atomic undo step.
 */
class AICommandBridge : private juce::AsyncUpdater
{
public:
    explicit AICommandBridge (EditManager& editManager);
    ~AICommandBridge() override;

    /** A single Edit mutation, executed on the message thread. Mutate via the passed
     *  UndoManager; do NOT begin a new transaction (the bridge owns the transaction). */
    using Command = std::function<void (juce::UndoManager&)>;

    /**
     * Enqueue a batch of commands. Callable from ANY thread (typically an AI worker).
     * The whole batch is applied on the message thread as ONE undo transaction
     * labelled `description`. Empty batches are ignored.
     */
    void enqueueBatch (const juce::String& description, std::vector<Command> commands);

    /** Convenience: enqueue a single command as its own one-command batch. */
    void enqueue (const juce::String& description, Command command);

    /**
     * Drain and apply every pending batch NOW. MUST be called on the message thread.
     * Invoked automatically when the AsyncUpdater fires; also exposed so callers and
     * tests can apply deterministically without spinning the message loop.
     */
    void applyPendingCommands();

    /** Number of batches still waiting to be applied. Thread-safe. */
    int getPendingBatchCount() const;

    /** Optional hook fired on the message thread after each batch is applied. */
    std::function<void (const juce::String& description)> onBatchApplied;

private:
    void handleAsyncUpdate() override;   // message thread → applyPendingCommands()

    struct Batch
    {
        juce::String         description;
        std::vector<Command> commands;
    };

    EditManager&                  editManager;
    mutable juce::CriticalSection queueLock;
    std::vector<Batch>            pending;   // guarded by queueLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AICommandBridge)
};
