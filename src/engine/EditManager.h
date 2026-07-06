#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_core/juce_core.h>
#include "AizanaUIBehaviour.h"
#include "WarpEngine.h"

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

    /** Load Edit from a .tracktion ValueTree XML file.
        Returns false (keeping the current Edit) if the file cannot be parsed —
        the caller should surface an error to the user instead of crashing. */
    bool loadEdit (const juce::File& file);

    /** Flush and save Edit state to a .tracktion ValueTree XML file. */
    void saveEdit (const juce::File& file);

    /**
     * Set the current session file path.
     * Once set, autosave writes {SessionName}_autosave.tracktionedit every 2 min.
     * Pass an invalid file to pause autosave (e.g., when no session is open).
     */
    void setCurrentFile (const juce::File& file);

    /** Returns the current session file (may be invalid if no file has been set). */
    juce::File getCurrentFile() const { return currentSessionFile; }

    /**
     * Returns the most recent autosave file for the given session, or an
     * invalid File if no autosave exists yet.
     */
    juce::File getLatestAutosave (const juce::File& sessionFile) const;

    /** Perform an autosave immediately (called by the timer and on clean exit).
        While the transport is PLAYING this writes a playback-safe snapshot (see
        writeAutosaveSnapshotAsync) instead of the full flush+save path — TE's
        save suspends every hosted plugin and drops audio-thread realtime priority,
        which made the whole mix audibly drop out on every timer tick (M13). */
    void performAutosave();

    /** Playback-safe autosave core: serialises an exclusively-owned deep copy of
        the edit state on a background thread and atomically swaps it into place.
        Never touches plugins, files or thread priorities of the audio engine.
        Public + static for direct regression testing. */
    static void writeAutosaveSnapshotAsync (juce::ValueTree exclusiveStateCopy,
                                            const juce::File& target);

    /**
     * Fired at the end of createNewEdit() and loadEdit(), after the new Edit is
     * fully constructed and its playback context is allocated. Lets owners react
     * to an Edit swap — e.g. InputBusManager seeds/applies Named Buses, since the
     * previous Edit's <INPUTBUSES> tree does not carry over to a fresh/loaded Edit.
     * Not fired by the startup createNewEdit() in ApplicationState (set after).
     */
    std::function<void()> onEditChanged;

    /**
     * Fired BEFORE the current Edit is destroyed on an Edit swap
     * (createNewEdit() / loadEdit()), while the old Edit is still fully alive.
     * UI owners MUST drop every reference into the old Edit here — remove
     * ValueTree listeners, destroy track/clip-bound components, close plugin
     * windows. Without this, the swap destroys tracks/plugins that views and
     * timers still reference → use-after-free (live-test crash on File>Open).
     */
    std::function<void()> onEditWillChange;

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
    te::Engine engine { "Aizana", std::make_unique<AizanaUIBehaviour>(), nullptr };
    std::unique_ptr<te::Edit> edit;

    // P7b: stößt bei jeder Warp-Änderung (auch Undo/Redo) den KORREKTEN
    // Warp-Render an, bevor TEs eigener stiller Job greift (WarpEngine-Doku).
    // Nach `edit` deklariert → wird VOR dem Edit zerstört (Auto-Detach, I-11);
    // zusätzlich explizit detacht, bevor loadEdit/createNewEdit swappen.
    WarpRenderWatcher warpRenderWatcher;

    // ---- Autosave (RESEARCH.md Open Question 4) ----
    juce::File currentSessionFile;  // set by setCurrentFile()

    // Autosave timer — fires every 2 minutes on the message thread
    struct AutosaveTimerImpl : public juce::Timer
    {
        EditManager& owner;
        explicit AutosaveTimerImpl (EditManager& o) : owner (o) {}
        void timerCallback() override { owner.performAutosave(); }
    };
    AutosaveTimerImpl autosaveTimer { *this };

    static constexpr int kAutosaveIntervalMs   = 2 * 60 * 1000;  // 2 minutes
    static constexpr int kAutosaveKeptCount    = 3;               // keep last 3

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditManager)
};
