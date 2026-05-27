// test_clip_editing.cpp — AUDIO-03: Non-destructive audio clip editing
//
// Tests ClipEditor: cut, trim, move, copy, and comping operations.
// All tests run headless (no display, no audio hardware required).
// Source audio files are NEVER modified by any clip operation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using TimeDur = tracktion::core::TimeDuration;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static te::AudioTrack* getFirstAudioTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    if (tracks.isEmpty())
        return nullptr;
    return tracks[0];
}

/** Insert a WaveAudioClip at a given time position without a real audio file.
    TE creates the clip in the ValueTree; no audio I/O occurs. */
static te::WaveAudioClip* insertDummyAudioClip (te::AudioTrack& track,
                                                  double startSecs, double lengthSecs,
                                                  juce::UndoManager* undoManager = nullptr)
{
    auto start = TimePos::fromSeconds (startSecs);
    auto end   = TimePos::fromSeconds (startSecs + lengthSecs);
    te::ClipPosition pos { TimeRng (start, end), {} };

    auto* clip = dynamic_cast<te::WaveAudioClip*> (
        track.insertNewClip (te::TrackItem::Type::wave, "clip", pos, nullptr));

    (void) undoManager;
    return clip;
}

/** Insert a MidiClip at a given time position. */
static te::MidiClip* insertDummyMidiClip (te::AudioTrack& track,
                                           double startSecs, double lengthSecs)
{
    auto start = TimePos::fromSeconds (startSecs);
    auto end   = TimePos::fromSeconds (startSecs + lengthSecs);
    te::ClipPosition pos { TimeRng (start, end), {} };

    auto* clip = dynamic_cast<te::MidiClip*> (
        track.insertNewClip (te::TrackItem::Type::midi, "clip", pos, nullptr));
    return clip;
}

// ---------------------------------------------------------------------------
// Test 1 — cutClip: 4-second clip cut at 2.0 s produces two clips [0,2] [2,4]
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: cutClip produces two clips at correct positions", "[clip][AUDIO-03]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    // Insert 4-second dummy audio clip at t=0
    auto* clip = insertDummyAudioClip (*track, 0.0, 4.0);
    REQUIRE (clip != nullptr);

    ClipEditor editor (em);
    auto* rightClip = editor.cutClip (*clip, 2.0);

    REQUIRE (rightClip != nullptr);

    // Left clip (original): should now span [0, 2]
    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (0.0, 0.001));
    REQUIRE_THAT (clip->getPosition().getEnd().inSeconds(),
                  Catch::Matchers::WithinAbs (2.0, 0.001));

    // Right clip (new): should span [2, 4]
    REQUIRE_THAT (rightClip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (2.0, 0.001));
    REQUIRE_THAT (rightClip->getPosition().getEnd().inSeconds(),
                  Catch::Matchers::WithinAbs (4.0, 0.001));
}

// ---------------------------------------------------------------------------
// Test 2 — trimClip: clip plays from 0.5 to 3.5, offset adjusted correctly
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: trimClip sets correct position and adjusts offset non-destructively", "[clip][AUDIO-03]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    // 4-second clip at t=0
    auto* clip = insertDummyAudioClip (*track, 0.0, 4.0);
    REQUIRE (clip != nullptr);

    double originalOffsetSecs = clip->getPosition().getOffset().inSeconds();  // 0.0

    ClipEditor editor (em);
    editor.trimClip (*clip, 0.5, 3.5);

    // Clip position should be [0.5, 3.5]
    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (0.5, 0.001));
    REQUIRE_THAT (clip->getPosition().getEnd().inSeconds(),
                  Catch::Matchers::WithinAbs (3.5, 0.001));

    // Offset must have increased by (newStart - originalStart) = 0.5 - 0.0 = 0.5
    double expectedOffset = originalOffsetSecs + (0.5 - 0.0);
    REQUIRE_THAT (clip->getPosition().getOffset().inSeconds(),
                  Catch::Matchers::WithinAbs (expectedOffset, 0.001));
}

// ---------------------------------------------------------------------------
// Test 3 — moveClip: clip moves to 8.0 s; undo reverts to original position
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: moveClip moves clip and undo reverts to original position", "[clip][AUDIO-03]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    auto* clip = insertDummyAudioClip (*track, 0.0, 1.0);
    REQUIRE (clip != nullptr);

    double originalStart = clip->getPosition().getStart().inSeconds();  // 0.0

    ClipEditor editor (em);
    editor.beginMoveTransaction();  // isolate move from clip-insertion in the undo stack
    editor.moveClip (*clip, 8.0);

    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (8.0, 0.001));

    // Undo → clip returns to original position
    REQUIRE (em.canUndo());
    em.undoLastAction();

    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (originalStart, 0.001));
}

// ---------------------------------------------------------------------------
// Test 4 — copyClip: new clip on track2 references same source; original unchanged
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: copyClip creates independent clip without modifying original", "[clip][AUDIO-03]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);

    // Add a second audio track to the edit
    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (tracks.size() >= 1);
    auto* track1 = tracks[0];
    REQUIRE (track1 != nullptr);

    // Insert original clip on track1
    auto* originalClip = insertDummyAudioClip (*track1, 1.0, 2.0);
    REQUIRE (originalClip != nullptr);

    double origStart = originalClip->getPosition().getStart().inSeconds();

    // Add a second track
    edit->getUndoManager().beginNewTransaction ("Add track");
    auto track2 = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, track1), nullptr);
    REQUIRE (track2 != nullptr);

    ClipEditor editor (em);
    auto* copiedClip = editor.copyClip (*originalClip, *track2, 0.0);

    REQUIRE (copiedClip != nullptr);

    // Original clip must be unchanged
    REQUIRE_THAT (originalClip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (origStart, 0.001));

    // Copied clip must be at t=0 on track2
    REQUIRE_THAT (copiedClip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (0.0, 0.001));

    // Both clips should have the same length
    REQUIRE_THAT (copiedClip->getPosition().getLength().inSeconds(),
                  Catch::Matchers::WithinAbs (originalClip->getPosition().getLength().inSeconds(), 0.001));
}

// ---------------------------------------------------------------------------
// Test 5 — Comping: insert 3 takes on a MidiClip; setActiveTake(1) → take 1 is current
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: setActiveTake selects the correct take on a MidiClip", "[clip][AUDIO-03]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    // Create a MidiClip that supports multi-take comping
    auto* midiClip = insertDummyMidiClip (*track, 0.0, 4.0);
    REQUIRE (midiClip != nullptr);

    // Add two extra takes so we have takes 0, 1, 2
    // (MidiClip::addTake adds a new sequence as a take)
    juce::MidiMessageSequence seq1, seq2;
    seq1.addEvent (juce::MidiMessage::noteOn  (1, 60, (uint8_t) 100), 0.0);
    seq1.addEvent (juce::MidiMessage::noteOff (1, 60, (uint8_t) 0),   0.5);
    seq2.addEvent (juce::MidiMessage::noteOn  (1, 64, (uint8_t) 80),  0.0);
    seq2.addEvent (juce::MidiMessage::noteOff (1, 64, (uint8_t) 0),   0.5);

    midiClip->addTake (seq1, te::MidiList::NoteAutomationType::none);
    midiClip->addTake (seq2, te::MidiList::NoteAutomationType::none);

    // Now the clip should have 3 takes (original + 2 added)
    REQUIRE (midiClip->getNumTakes (false) >= 2);

    ClipEditor editor (em);

    // Set to take 1
    editor.setActiveTake (*midiClip, 1);
    REQUIRE (editor.getActiveTake (*midiClip) == 1);

    // Set back to take 0
    editor.setActiveTake (*midiClip, 0);
    REQUIRE (editor.getActiveTake (*midiClip) == 0);
}
