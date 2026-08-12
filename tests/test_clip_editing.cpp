// test_clip_editing.cpp — AUDIO-03: Non-destructive audio clip editing
//
// Tests ClipEditor: cut, trim, move, copy, and comping operations.
// All tests run headless (no display, no audio hardware required).
// Source audio files are NEVER modified by any clip operation.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>

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
// Namen: Schnitt-Haelften, Kopien und Duplikate erben den Namen des Originals.
// Vorher hiess jedes neu eingefuegte Stueck "clip" — nach zwei Schnitten stand in
// der Spur "Vocal Lead", "clip", "clip", und die Herkunft war nicht mehr ablesbar.
// ---------------------------------------------------------------------------
TEST_CASE ("ClipEditor: Schnitt, Kopie und Duplikat erben den Clipnamen", "[clip][naming]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);

    auto* track = getFirstAudioTrack (*edit);
    REQUIRE (track != nullptr);

    auto* original = insertDummyAudioClip (*track, 0.0, 4.0);
    REQUIRE (original != nullptr);
    original->setName ("Vocal Lead");

    ClipEditor editor (em);

    auto* rightHalf = editor.cutClip (*original, 2.0);
    REQUIRE (rightHalf != nullptr);
    CHECK (rightHalf->getName() == "Vocal Lead");
    CHECK (original->getName()  == "Vocal Lead");   // linke Haelfte unveraendert

    auto* duplicate = editor.duplicateClip (*original);
    REQUIRE (duplicate != nullptr);
    CHECK (duplicate->getName() == "Vocal Lead");

    // Kopie auf eine ANDERE Spur — derselbe Kern (copyClip), anderer Aufrufweg.
    edit->getUndoManager().beginNewTransaction ("Add track");
    auto track2 = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, track), nullptr);
    REQUIRE (track2 != nullptr);
    auto* copy = editor.copyClip (*original, *track2, 0.0);
    REQUIRE (copy != nullptr);
    CHECK (copy->getName() == "Vocal Lead");
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

// ---------------------------------------------------------------------------
// Multi-selection delete (User-Befund 2026-07-05: Rechtsklick→Delete löschte
// nur EINEN von mehreren selektierten Schnipseln; Delete-Taste ging gar nicht)
// ---------------------------------------------------------------------------

TEST_CASE ("ClipEditor: deleteClips removes a whole batch in ONE undo step", "[clip][delete]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    auto* a = insertDummyAudioClip (*track, 0.0, 2.0);
    auto* b = insertDummyAudioClip (*track, 3.0, 2.0);
    auto* c = insertDummyAudioClip (*track, 6.0, 2.0);
    REQUIRE (a != nullptr);
    REQUIRE (b != nullptr);
    REQUIRE (c != nullptr);
    REQUIRE (track->getClips().size() == 3);

    ClipEditor editor (em);
    editor.deleteClips ({ a, b, c });

    CHECK (track->getClips().size() == 0);

    // The whole batch must be ONE Cmd+Z step: a single undo restores all three.
    em.undoLastAction();
    CHECK (track->getClips().size() == 3);
}

TEST_CASE ("ClipEditor: deleteClips skips null entries and empty batches safely", "[clip][delete]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    auto* a = insertDummyAudioClip (*track, 0.0, 2.0);
    REQUIRE (a != nullptr);

    ClipEditor editor (em);
    editor.deleteClips ({});                       // no-op
    CHECK (track->getClips().size() == 1);

    editor.deleteClips ({ nullptr, a, nullptr }); // nulls skipped, a deleted
    CHECK (track->getClips().size() == 0);
}

// ---------------------------------------------------------------------------
// removeRangeFromClip — der S1-Atmer-Workflow im Docked Editor (E2):
// Bereich markieren → Delete → Lücke bleibt, Audio bleibt quellsynchron, 1 Undo.
// ---------------------------------------------------------------------------

TEST_CASE ("ClipEditor: removeRangeFromClip cuts a hole in ONE undo step, source-synced", "[clip][range]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    // Clip [1.0 .. 9.0], Offset 0.5 in die Quelldatei
    auto* clip = insertDummyAudioClip (*track, 1.0, 8.0);
    REQUIRE (clip != nullptr);
    clip->setPosition ({ TimeRng (TimePos::fromSeconds (1.0), TimePos::fromSeconds (9.0)),
                         TimeDur::fromSeconds (0.5) });
    REQUIRE (track->getClips().size() == 1);

    ClipEditor editor (em);
    auto* right = editor.removeRangeFromClip (*clip, 3.0, 5.0);   // Loch [3..5]

    REQUIRE (right != nullptr);
    REQUIRE (track->getClips().size() == 2);

    // Links: [1.0 .. 3.0], Offset unverändert
    CHECK (clip->getPosition().getStart().inSeconds() == Catch::Approx (1.0));
    CHECK (clip->getPosition().getEnd().inSeconds()   == Catch::Approx (3.0));
    CHECK (clip->getPosition().getOffset().inSeconds() == Catch::Approx (0.5));

    // Rechts: [5.0 .. 9.0] — Offset = 0.5 + (5.0 - 1.0) = 4.5 → Audio bleibt synchron
    CHECK (right->getPosition().getStart().inSeconds() == Catch::Approx (5.0));
    CHECK (right->getPosition().getEnd().inSeconds()   == Catch::Approx (9.0));
    CHECK (right->getPosition().getOffset().inSeconds() == Catch::Approx (4.5));

    // EIN Undo stellt den Originalzustand her (eine Geste = ein Cmd+Z)
    em.undoLastAction();
    REQUIRE (track->getClips().size() == 1);
    auto* restored = track->getClips().getFirst();
    CHECK (restored->getPosition().getStart().inSeconds() == Catch::Approx (1.0));
    CHECK (restored->getPosition().getEnd().inSeconds()   == Catch::Approx (9.0));
}

TEST_CASE ("ClipEditor: removeRangeFromClip edge overlaps trim, full cover deletes", "[clip][range]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    ClipEditor editor (em);

    SECTION ("Range über den Clip-Anfang → Kopf wird getrimmt (Offset rückt nach)")
    {
        auto* clip = insertDummyAudioClip (*track, 2.0, 4.0);   // [2..6]
        REQUIRE (editor.removeRangeFromClip (*clip, 0.0, 3.0) == nullptr);
        CHECK (clip->getPosition().getStart().inSeconds() == Catch::Approx (3.0));
        CHECK (clip->getPosition().getEnd().inSeconds()   == Catch::Approx (6.0));
        CHECK (clip->getPosition().getOffset().inSeconds() == Catch::Approx (1.0));
    }

    SECTION ("Range über das Clip-Ende → Schwanz wird getrimmt")
    {
        auto* clip = insertDummyAudioClip (*track, 2.0, 4.0);   // [2..6]
        REQUIRE (editor.removeRangeFromClip (*clip, 5.0, 8.0) == nullptr);
        CHECK (clip->getPosition().getStart().inSeconds() == Catch::Approx (2.0));
        CHECK (clip->getPosition().getEnd().inSeconds()   == Catch::Approx (5.0));
    }

    SECTION ("Range deckt den ganzen Clip → Clip wird gelöscht")
    {
        auto* clip = insertDummyAudioClip (*track, 2.0, 4.0);
        const int before = track->getClips().size();
        editor.removeRangeFromClip (*clip, 1.0, 7.0);
        CHECK (track->getClips().size() == before - 1);
    }

    SECTION ("Keine Überlappung → No-op")
    {
        auto* clip = insertDummyAudioClip (*track, 2.0, 4.0);
        REQUIRE (editor.removeRangeFromClip (*clip, 8.0, 9.0) == nullptr);
        CHECK (clip->getPosition().getStart().inSeconds() == Catch::Approx (2.0));
        CHECK (clip->getPosition().getEnd().inSeconds()   == Catch::Approx (6.0));
    }
}
