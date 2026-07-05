#pragma once

#include "EditManager.h"

/**
 * ClipEditor — Non-destructive clip operations over Tracktion Engine's clip model.
 *
 * THREAD CONSTRAINT: All ClipEditor methods MUST be called from the message thread only.
 * These are editing operations that mutate the Edit's ValueTree and therefore cannot
 * be called from the audio callback (processBlock). Calling any method from the audio
 * thread is undefined behaviour.
 *
 * All operations route through EditManager::getUndoManager(), making them individually
 * undoable (or part of an AI batch transaction if beginAITransaction() was called first).
 *
 * Audio file non-destructive guarantee: source audio files are NEVER modified by any
 * clip operation. cutClip, trimClip, copyClip, and compClips all operate on the
 * ValueTree/metadata level only. The source file path/reference is preserved as-is.
 */
class ClipEditor
{
public:
    explicit ClipEditor (EditManager& editManager);

    // -------------------------------------------------------------------------
    // Non-destructive clip operations — each begins a new UndoManager transaction
    // so they are individually undoable via EditManager::undoLastAction().
    // -------------------------------------------------------------------------

    /**
     * Cut a WaveAudioClip at splitTime, producing two clips.
     *
     * The original clip is trimmed to [originalStart, splitTime].
     * A new clip is created at [splitTime, originalEnd] referencing the same audio file
     * with the correct offset so the audio content is contiguous.
     *
     * Returns a pointer to the NEW (right-side) clip, or nullptr on failure.
     * The original clip (left side) is modified in place.
     *
     * Source audio file: NEVER modified. Only ValueTree positions change.
     */
    te::WaveAudioClip* cutClip (te::WaveAudioClip& clip, double splitTimeSecs);

    /**
     * Trim a clip to [newStartTimeSecs, newEndTimeSecs].
     *
     * For audio clips, the internal offset is adjusted so the audio content plays
     * from the correct position in the source file (non-destructive).
     *
     * Source audio file: NEVER modified. Only position/offset ValueTree properties change.
     */
    void trimClip (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs);

    /**
     * Move a clip to start at newStartTimeSecs (preserving clip length).
     *
     * Does NOT begin an undo transaction — call beginMoveTransaction() before
     * starting a drag so the entire drag resolves to a single undo step.
     *
     * Source audio file: NEVER modified.
     */
    void moveClip (te::Clip& clip, double newStartTimeSecs);

    /**
     * Begin an undo transaction for a drag-based clip move.
     * Call once from ClipComponent::mouseDown (pointer tool) before the first
     * moveClip() so all mid-drag position updates share one undo step.
     */
    void beginMoveTransaction();

    /**
     * Copy a clip to destTrack, starting at startTimeSecs.
     *
     * Creates a new clip in destTrack's ValueTree that references the SAME audio file
     * as the source clip (for WaveAudioClips). The source clip and source file are
     * both unchanged.
     *
     * Returns a pointer to the new clip, or nullptr on failure.
     */
    te::Clip* copyClip (te::Clip& sourceClip, te::AudioTrack& destTrack, double startTimeSecs);

    /**
     * Duplicate a clip — creates a copy immediately after the original on the same track.
     * The copy starts at the original clip's end time.
     * Returns pointer to the new clip, or nullptr on failure.
     */
    te::Clip* duplicateClip (te::Clip& clip);

    /**
     * Paste a previously copied clip onto destTrack at startTimeSecs.
     * Returns pointer to the pasted clip, or nullptr if clipboard is empty.
     * ClipEditor owns a simple one-slot clipboard (last Cmd+C).
     */
    te::Clip* pasteClip (te::AudioTrack& destTrack, double startTimeSecs);

    /** Copy clip reference to the internal one-slot clipboard. */
    void copyToClipboard (te::Clip& clip);

    /** True if the clipboard holds a clip reference. */
    bool hasClipboardContents() const noexcept { return clipboardClip != nullptr; }

    /** Drop the clipboard reference. MUST be called on an Edit swap (File>New/Open) —
        clipboardClip is a raw pointer into the old Edit; pasting it after the swap
        would dereference a destroyed clip. */
    void clearClipboard() noexcept
    {
        clipboardClip        = nullptr;
        clipboardSourceTrack = nullptr;
        clipboardState       = {};
    }

    /**
     * Move a clip to a different track, preserving its start time.
     * Uses te::Clip::moveTo() which handles all clip types (audio + MIDI).
     * The operation is undoable.
     */
    void moveClipToTrack (te::Clip& clip, te::AudioTrack& destTrack);

    // -------------------------------------------------------------------------
    // Comping — multi-take workflow
    // Takes on a MidiClip use MidiClip::setCurrentTake().
    // Takes on a WaveAudioClip use WaveAudioClip::setCurrentTake().
    // -------------------------------------------------------------------------

    /**
     * Set the active (audible) take for a multi-take clip.
     *
     * Uses the clip's setCurrentTake() API — the underlying engine manages
     * muting/unmuting take lanes accordingly.
     *
     * @param takeIndex  Zero-based index of the take to make active.
     */
    void setActiveTake (te::Clip& comp, int takeIndex);

    /** Returns the currently active take index (zero-based). */
    int getActiveTake (const te::Clip& comp) const;

    // -------------------------------------------------------------------------
    // Timestretch — conform a clip to the song tempo (Phase 3, Task 0)
    // Requires a timestretch backend (TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1),
    // otherwise TE silently plays the clip unstretched.
    // -------------------------------------------------------------------------

    /**
     * Timestretch a clip so it follows the song tempo (and future tempo changes).
     *
     * Stores sourceBpm in the clip's LoopInfo, enables SoundTouch stretch mode +
     * autoTempo, and rescales the clip length by sourceBpm/songBpm so the same
     * musical material stays covered. One undo step.
     *
     * Source audio file: NEVER modified (stretching happens at playback/render).
     *
     * Returns false if the source file is unreadable or a tempo is invalid.
     */
    bool conformToSongTempo (te::WaveAudioClip& clip, double sourceBpm);

    /**
     * Stop a clip following the song tempo and restore its natural playback
     * length (inverse of conformToSongTempo). One undo step.
     * Returns false if autoTempo was not enabled on the clip.
     */
    bool removeSongTempoConform (te::WaveAudioClip& clip);

    // -------------------------------------------------------------------------
    // Audio file import — creates a WaveAudioClip from a file on disk
    // -------------------------------------------------------------------------

    /**
     * Delete a clip from its parent track (undoable).
     */
    void deleteClip (te::Clip& clip);

    /**
     * Delete a batch of clips in ONE undo transaction (multi-selection delete).
     * Null entries are skipped; clips are kept alive for the whole removal loop.
     */
    void deleteClips (const juce::Array<te::Clip*>& clips);

    /**
     * Remove [rangeStart, rangeEnd] (edit-absolute seconds) from ONE clip in a
     * single undo step — the Studio One breath-edit workflow (mark range, remove,
     * the gap stays). Hole inside the clip → split + right piece re-trimmed with
     * source sync; edge overlap → trim; full cover → clip deleted.
     * Returns the newly created right-hand piece, if one was created.
     */
    te::WaveAudioClip* removeRangeFromClip (te::WaveAudioClip& clip,
                                            double rangeStartSecs, double rangeEndSecs);

    /**
     * Import an audio file as a new WaveAudioClip on the given track.
     *
     * The clip's start time is startTimeSecs. Its duration is determined from
     * the actual audio file length (via JUCE AudioFormatManager).
     *
     * The operation is wrapped in an UndoManager transaction so it is undoable
     * with Cmd+Z.
     *
     * Returns a pointer to the created clip, or nullptr on failure (unrecognised
     * format, bad file, TE insertion error).
     *
     * Source audio file: NEVER modified. The clip references the file via
     * setToDirectFileReference() (non-destructive).
     */
    te::WaveAudioClip* importAudioFile (const juce::File& audioFile,
                                        te::AudioTrack&   track,
                                        double            startTimeSecs);

private:
    EditManager& editManager;

    // Transaction-free cores (house pattern): the public cutClip/trimClip wrap these
    // in their own transaction; composite gestures (removeRangeFromClip) call the
    // cores directly so the whole gesture stays ONE undo step.
    te::WaveAudioClip* cutClipCore  (te::WaveAudioClip& clip, double splitTimeSecs);
    void               trimClipCore (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs);

    // One-slot clipboard — stores a reference to the last copied clip.
    // Weak pointer pattern: store the clip's juce::ValueTree (always valid until clip deleted).
    juce::ValueTree  clipboardState;
    te::AudioTrack*  clipboardSourceTrack = nullptr;  // track the clipboard clip came from
    te::Clip*        clipboardClip        = nullptr;  // raw ptr — checked against edit's clips before use

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipEditor)
};
