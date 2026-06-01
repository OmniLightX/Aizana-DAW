#include "AICommandBridge.h"

//==============================================================================
AICommandBridge::AICommandBridge (EditManager& em)
    : editManager (em)
{
}

AICommandBridge::~AICommandBridge()
{
    // Ensure no queued handleAsyncUpdate() fires after we start destructing.
    cancelPendingUpdate();
}

//==============================================================================
void AICommandBridge::enqueueBatch (const juce::String& description,
                                    std::vector<Command> commands)
{
    if (commands.empty())
        return;

    {
        const juce::ScopedLock sl (queueLock);
        pending.push_back ({ description, std::move (commands) });
    }

    // Safe to call from any thread — the handler is dispatched on the message thread.
    triggerAsyncUpdate();
}

void AICommandBridge::enqueue (const juce::String& description, Command command)
{
    std::vector<Command> one;
    one.push_back (std::move (command));
    enqueueBatch (description, std::move (one));
}

//==============================================================================
void AICommandBridge::applyPendingCommands()
{
    // Move the pending batches out under the lock so the AI thread can keep enqueuing
    // while we apply. The commands themselves run WITHOUT holding the lock.
    std::vector<Batch> toApply;
    {
        const juce::ScopedLock sl (queueLock);
        toApply.swap (pending);
    }

    if (toApply.empty())
        return;

    // No Edit to mutate (e.g. between sessions) — drop the batches rather than crash.
    if (editManager.getEdit() == nullptr)
        return;

    auto& um = editManager.getUndoManager();

    for (auto& batch : toApply)
    {
        // ONE transaction per batch → the whole AI action collapses to a single
        // undo step. Commands must not begin their own transaction (see header).
        editManager.beginAITransaction (batch.description);

        for (auto& cmd : batch.commands)
            if (cmd)
                cmd (um);

        if (onBatchApplied)
            onBatchApplied (batch.description);
    }
}

int AICommandBridge::getPendingBatchCount() const
{
    const juce::ScopedLock sl (queueLock);
    return (int) pending.size();
}

void AICommandBridge::handleAsyncUpdate()
{
    applyPendingCommands();
}
