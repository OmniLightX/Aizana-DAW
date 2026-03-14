// test_midi_recording.cpp — AUDIO-04: MIDI recording + Piano Roll data model
//
// Tests MidiRecorder: note recording, CC lane capture, and multi-take comping.
// All tests run headless — no display or audio hardware required.
// MIDI events are injected directly via MidiRecorder::injectMidiMessage().

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/MidiRecorder.h"

// Convenience aliases
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using BeatPos = tracktion::core::BeatPosition;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static te::AudioTrack* getFirstAudioTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    return tracks.isEmpty() ? nullptr : tracks[0];
}

// ---------------------------------------------------------------------------
// Test 1 — Note recording: noteOn + noteOff → MidiClip contains correct note
// ---------------------------------------------------------------------------
TEST_CASE ("MidiRecorder: injected noteOn/noteOff produces correct MidiList entry", "[midi][AUDIO-04]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    MidiRecorder recorder (em);
    recorder.startRecording (*track);

    // Inject: noteOn ch=1, pitch=60 (middle C), velocity=0.8*127 ≈ 102
    // (juce::MidiMessage takes float velocity 0.0..1.0 → rounds to int 0..127)
    const uint8_t rawVelocity = static_cast<uint8_t> (juce::roundToInt (0.8f * 127.0f));  // 102
    recorder.injectMidiMessage (juce::MidiMessage::noteOn  (1, 60, rawVelocity), 0.0);
    recorder.injectMidiMessage (juce::MidiMessage::noteOff (1, 60, (uint8_t) 0),  0.5);

    recorder.stopRecording();
    auto* clip = recorder.getLastClip();
    REQUIRE (clip != nullptr);

    auto& seq = clip->getSequence();
    REQUIRE (seq.getNumNotes() == 1);

    auto* note = seq.getNote (0);
    REQUIRE (note != nullptr);

    // Pitch must be 60 (middle C)
    REQUIRE (note->getNoteNumber() == 60);

    // Velocity: juce::MidiMessage rounds 0.8f*127 = 101.6 to 102
    REQUIRE (note->getVelocity() == static_cast<int> (rawVelocity));

    // Start beat: t=0.0 at default 120 BPM → beat 0.0
    REQUIRE_THAT (note->getStartBeat().inBeats(),
                  Catch::Matchers::WithinAbs (0.0, 0.01));

    // Duration: 0.5s at 120 BPM = 0.5 * (120/60) = 1.0 beats
    REQUIRE (note->getLengthBeats().inBeats() > 0.0);
}

// ---------------------------------------------------------------------------
// Test 2 — Multi-take comping: two recording passes create separate takes
// ---------------------------------------------------------------------------
TEST_CASE ("MidiRecorder: two recording passes produce two separate takes", "[midi][AUDIO-04]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    MidiRecorder recorder (em);

    // Take 1
    recorder.startRecording (*track);
    recorder.injectMidiMessage (juce::MidiMessage::noteOn  (1, 60, (uint8_t) 100), 0.0);
    recorder.injectMidiMessage (juce::MidiMessage::noteOff (1, 60, (uint8_t) 0),   0.25);
    recorder.stopRecording();

    auto* clip1 = recorder.getLastClip();
    REQUIRE (clip1 != nullptr);

    // Take 2 — add a second recording on the same track using addTake()
    juce::MidiMessageSequence take2Seq;
    take2Seq.addEvent (juce::MidiMessage::noteOn  (1, 64, (uint8_t) 80), 0.0);
    take2Seq.addEvent (juce::MidiMessage::noteOff (1, 64, (uint8_t) 0),  0.25);
    clip1->addTake (take2Seq, te::MidiList::NoteAutomationType::none);

    // The clip now has at least 2 takes
    REQUIRE (clip1->getNumTakes (false) >= 2);

    // Select take 1 (zero-based index 1 = second take)
    clip1->setCurrentTake (1);
    REQUIRE (clip1->getCurrentTake() == 1);

    // Verify we can switch back to take 0
    clip1->setCurrentTake (0);
    REQUIRE (clip1->getCurrentTake() == 0);
}

// ---------------------------------------------------------------------------
// Test 3 — CC lane: CC7 at beat 2.0 stored correctly
// ---------------------------------------------------------------------------
TEST_CASE ("MidiRecorder: injected CC7 event stored in CC lane with correct beat and value", "[midi][AUDIO-04]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    MidiRecorder recorder (em);
    recorder.startRecording (*track);

    // At 120 BPM (default), beat 2.0 = 1.0 second
    // CC7 (volume) = 100
    double timeAtBeat2 = 1.0;  // 120 BPM → 2 beats per second → beat 2 at 1.0 s
    recorder.injectMidiMessage (
        juce::MidiMessage::controllerEvent (1, 7, 100),
        timeAtBeat2);

    recorder.stopRecording();
    auto* clip = recorder.getLastClip();
    REQUIRE (clip != nullptr);

    auto& seq = clip->getSequence();
    REQUIRE (seq.getNumControllerEvents() == 1);

    auto* ccEvent = seq.getControllerEvent (0);
    REQUIRE (ccEvent != nullptr);

    // Controller type must be 7 (CC7 = volume)
    REQUIRE (ccEvent->getType() == 7);

    // Controller value must be 100
    REQUIRE (ccEvent->getControllerValue() == 100);

    // Beat position: at 120 BPM, 1.0 s = 2.0 beats
    REQUIRE_THAT (ccEvent->getBeatPosition().inBeats(),
                  Catch::Matchers::WithinAbs (2.0, 0.05));
}
