#include "MidiRecorder.h"

// Convenience aliases for Tracktion's strong time types
using TimePos  = tracktion::core::TimePosition;
using TimeRng  = tracktion::core::TimeRange;
using TimeDur  = tracktion::core::TimeDuration;
using BeatPos  = tracktion::core::BeatPosition;
using BeatDur  = tracktion::core::BeatDuration;

//==============================================================================
MidiRecorder::MidiRecorder (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
void MidiRecorder::startRecording (te::AudioTrack& track)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Start MIDI recording");

    // Start the new clip at the transport's current position.
    // If the transport isn't running, default to 0.0.
    double startSecs = edit->getTransport().getPosition().inSeconds();

    // Give the clip a non-zero initial length (will be corrected on stopRecording).
    // A zero-length clip may cause issues with some Tracktion Engine internals.
    double initialLengthSecs = 30.0;

    auto clipStart = TimePos::fromSeconds (startSecs);
    auto clipEnd   = TimePos::fromSeconds (startSecs + initialLengthSecs);
    te::ClipPosition pos { TimeRng (clipStart, clipEnd), {} };

    currentClip = dynamic_cast<te::MidiClip*> (
        track.insertNewClip (te::TrackItem::Type::midi, "Recording", pos, nullptr));

    pendingNotes.clear();
    recording = true;
}

//==============================================================================
void MidiRecorder::stopRecording()
{
    if (! recording || currentClip == nullptr)
    {
        recording = false;
        return;
    }

    auto* edit = editManager.getEdit();
    if (edit != nullptr)
    {
        // Finalise the clip's end time at the transport's current position.
        double endSecs = edit->getTransport().getPosition().inSeconds();
        double startSecs = currentClip->getPosition().getStart().inSeconds();

        // Ensure at least a minimal clip length even if transport didn't advance.
        if (endSecs <= startSecs)
            endSecs = startSecs + 0.001;

        auto clipStart = TimePos::fromSeconds (startSecs);
        auto clipEnd   = TimePos::fromSeconds (endSecs);
        currentClip->setPosition ({ TimeRng (clipStart, clipEnd), {} });
    }

    // Close any pending notes (noteOn without matching noteOff)
    pendingNotes.clear();
    recording = false;
}

//==============================================================================
te::MidiClip* MidiRecorder::getLastClip() const
{
    return currentClip;
}

//==============================================================================
tracktion::core::BeatPosition MidiRecorder::toBeatPosition (double timeSecs) const
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return BeatPos();

    // Convert edit-time seconds to beats using the edit's tempo map.
    return edit->tempoSequence.toBeats (TimePos::fromSeconds (timeSecs));
}

//==============================================================================
void MidiRecorder::injectMidiMessage (const juce::MidiMessage& msg, double timeSecs)
{
    if (! recording || currentClip == nullptr)
        return;

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& undoManager = editManager.getUndoManager();
    // Note: we do NOT call beginNewTransaction here — injected events are part
    // of the ongoing recording transaction started in startRecording().

    BeatPos eventBeat = toBeatPosition (timeSecs);

    if (msg.isNoteOn())
    {
        // Store pending noteOn — will be finalised when matching noteOff arrives
        PendingNote pn;
        pn.pitch     = msg.getNoteNumber();
        pn.velocity  = msg.getVelocity();
        pn.startSecs = timeSecs;
        pendingNotes.add (pn);
    }
    else if (msg.isNoteOff())
    {
        int pitch = msg.getNoteNumber();

        // Find and close the matching pending noteOn
        for (int i = pendingNotes.size() - 1; i >= 0; --i)
        {
            if (pendingNotes[i].pitch == pitch)
            {
                PendingNote pn = pendingNotes[i];
                pendingNotes.remove (i);

                BeatPos startBeat = toBeatPosition (pn.startSecs);
                BeatPos endBeat   = eventBeat;

                if (endBeat > startBeat)
                {
                    BeatDur duration { endBeat - startBeat };
                    currentClip->getSequence().addNote (pn.pitch,
                                                        startBeat,
                                                        duration,
                                                        pn.velocity,
                                                        0,   // colour index
                                                        &undoManager);
                }
                break;
            }
        }
    }
    else if (msg.isController())
    {
        int controllerType  = msg.getControllerNumber();
        int controllerValue = msg.getControllerValue();

        currentClip->getSequence().addControllerEvent (eventBeat,
                                                        controllerType,
                                                        controllerValue,
                                                        &undoManager);
    }
}
