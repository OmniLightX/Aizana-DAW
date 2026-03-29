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
     * Equivalent to EditManager::moveClip but provided here for symmetry with
     * other ClipEditor operations.
     *
     * Source audio file: NEVER modified.
     */
    void moveClip (te::Clip& clip, double newStartTimeSecs);

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
    // Audio file import — creates a WaveAudioClip from a file on disk
    // -------------------------------------------------------------------------

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

    // One-slot clipboard — stores a reference to the last copied clip.
    // Weak pointer pattern: store the clip's juce::ValueTree (always valid until clip deleted).
    juce::ValueTree  clipboardState;
    te::AudioTrack*  clipboardSourceTrack = nullptr;  // track the clipboard clip came from
    te::Clip*        clipboardClip        = nullptr;  // raw ptr — checked against edit's clips before use

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipEditor)
};
