#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_core/juce_core.h>

namespace te = tracktion::engine;

/**
 * EditManager — Central facade over tracktion::engine::Engine and Edit.
 *
 * Design invariants (from CONTEXT.md / RESEARCH.md):
 *   - ONE shared UndoManager for the entire Edit (from edit->getUndoManager())
 *   - NEVER create a separate local UndoManager — all mutations must route through
 *     the shared one for correct AI batch undo grouping
 *   - AI actions: call beginAITransaction() once before the first mutation;
 *     subsequent mutations in the batch collapse into one undo step
 *   - User edits: each moveClip() / trimClip() call begins its own transaction
 *
 * Session format: Tracktion Engine .tracktion (ValueTree XML)
 */
class EditManager
{
public:
    explicit EditManager();
    ~EditManager();

    // --- Session lifecycle ---

    /** Create a fresh Edit with one audio track. Transport is stopped. */
    void createNewEdit();

    /** Load Edit from a .tracktion ValueTree XML file. */
    void loadEdit (const juce::File& file);

    /** Flush and save Edit state to a .tracktion ValueTree XML file. */
    void saveEdit (const juce::File& file);

    // --- AI-aware undo (AUDIO-05) ---

    /**
     * Begin an AI batch transaction.
     * All mutations between this call and the next beginAITransaction() /
     * user action collapse into a single undo step.
     */
    void beginAITransaction (const juce::String& description);

    /** Undo the last transaction (may revert one user edit OR one AI batch). */
    void undoLastAction();

    /** Redo the previously undone transaction. */
    void redoLastAction();

    bool canUndo() const;
    bool canRedo() const;

    // --- Accessors ---

    te::Edit*  getEdit()        { return edit.get(); }
    te::Engine& getEngine()    { return engine; }

    /**
     * Returns the single shared UndoManager for this Edit.
     * CRITICAL: all ValueTree mutations throughout the application must use
     * this exact UndoManager — never a separate one.
     */
    juce::UndoManager& getUndoManager();

    // --- User edit helpers (each begins a new transaction) ---

    /** Move clip to newStartTime. Records one undo step per call. */
    void moveClip (te::Clip& clip, double newStartTime);

    /** Trim clip start and end. Records one undo step per call. */
    void trimClip (te::Clip& clip, double newStartTime, double newEndTime);

private:
    te::Engine engine { "Cloudes" };
    std::unique_ptr<te::Edit> edit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditManager)
};
