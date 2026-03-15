#pragma once

#include "EditManager.h"

/**
 * MidiRecorder — Records MIDI events into a MidiClip with multi-take comping support.
 *
 * THREAD CONSTRAINT: All methods must be called from the message thread only.
 *
 * Recording workflow:
 *   1. Call startRecording(track) — creates a MidiClip and sets up for recording.
 *   2. During recording, MIDI events arrive via injectMidiMessage() (for testing)
 *      or from the hardware MIDI input via Tracktion Engine's record transport.
 *   3. Call stopRecording() — finalises the clip time range.
 *   4. Call getLastClip() to retrieve the recorded MidiClip.
 *
 * Comping support:
 *   Multiple takes on the same track are supported via MidiClip::addTake().
 *   Use ClipEditor::setActiveTake() to switch between takes.
 *
 * CC lane support:
 *   CC events injected via injectMidiMessage() are stored in the MidiClip's
 *   sequence using MidiList::addControllerEvent().
 *
 * Headless testing:
 *   injectMidiMessage() bypasses hardware completely, allowing fully headless tests.
 */
class MidiRecorder
{
public:
    explicit MidiRecorder (EditManager& editManager);

    /**
     * Arm a track for MIDI recording and create a new MidiClip starting at the
     * edit's current transport position.
     *
     * @param track  The AudioTrack to record into.
     */
    void startRecording (te::AudioTrack& track);

    /**
     * Stop recording and finalise the MidiClip's time range.
     *
     * The clip's end is set to the transport's current position at stop time.
     * If no events were recorded, the clip still exists but is empty.
     */
    void stopRecording();

    /** Returns the most recently finalised MidiClip, or nullptr if none. */
    te::MidiClip* getLastClip() const;

    /**
     * Inject a MIDI message directly into the current recording clip.
     * Bypasses audio hardware — intended for headless unit testing.
     *
     * For noteOn/noteOff pairs: automatically pairs them and adds a MidiNote.
     * For CC messages: adds a MidiControllerEvent at the converted beat position.
     *
     * @param msg         The MIDI message to inject.
     * @param timeSecs    The time of the event in edit seconds.
     */
    void injectMidiMessage (const juce::MidiMessage& msg, double timeSecs);

private:
    EditManager& editManager;
    te::MidiClip* currentClip = nullptr;
    bool recording = false;

    // Pending noteOn events awaiting a matching noteOff for note pairing
    struct PendingNote
    {
        int pitch;
        int velocity;
        double startSecs;
    };
    juce::Array<PendingNote> pendingNotes;

    /** Convert edit-time seconds to beat position using the edit's tempo map. */
    tracktion::core::BeatPosition toBeatPosition (double timeSecs) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRecorder)
};
