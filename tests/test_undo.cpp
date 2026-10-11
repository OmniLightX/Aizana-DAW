// test_undo.cpp — AUDIO-05: Unlimited undo/redo including AI action grouping
//
// Tests the shared juce::UndoManager that routes all ValueTree mutations.
// AI-aware grouping: multiple track mutations from one AI action must collapse
// into a single undo step via UndoManager::beginNewTransaction().
//
// All tests run headless (no display, no audio hardware required).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;

// ---------------------------------------------------------------------------
// Helper: retrieve the first audio track from the edit
// ---------------------------------------------------------------------------
static te::AudioTrack* getFirstAudioTrack (te::Edit& edit)
{
    auto tracks = te::getAudioTracks (edit);
    if (tracks.isEmpty())
        return nullptr;
    return tracks[0];
}

// ---------------------------------------------------------------------------
// Helper: insert a WaveAudioClip at the given position (no real file needed
// for undo tests — TE creates the clip in the ValueTree without reading audio).
// ---------------------------------------------------------------------------
static te::WaveAudioClip* insertDummyClip (te::AudioTrack& track,
                                            double startSecs, double lengthSecs)
{
    auto start = TimePos::fromSeconds (startSecs);
    auto end   = TimePos::fromSeconds (startSecs + lengthSecs);
    te::ClipPosition pos { TimeRng (start, end), {} };

    auto* clip = dynamic_cast<te::WaveAudioClip*> (
        track.insertNewClip (te::TrackItem::Type::wave, "clip", pos, nullptr));
    return clip;
}

// ---------------------------------------------------------------------------
// Test 1 — createNewEdit: Edit exists, >=1 track, transport stopped
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: createNewEdit produces a valid stopped Edit", "[undo][AUDIO-05]")
{
    EditManager em;
    em.createNewEdit();

    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);

    auto tracks = te::getAudioTracks (*edit);
    REQUIRE (tracks.size() >= 1);

    REQUIRE_FALSE (edit->getTransport().isPlaying());
}

// ---------------------------------------------------------------------------
// Test 2 — Single user moveClip is undone by one undo()
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: moveClip undo reverts clip to original position", "[undo][AUDIO-05]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    auto* clip = insertDummyClip (*track, 0.0, 1.0);
    REQUIRE (clip != nullptr);

    double originalStart = clip->getPosition().getStart().inSeconds();

    // Move clip to 2.0 seconds (records one undo step)
    em.moveClip (*clip, 2.0);

    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (2.0, 0.001));

    // Undo -> should revert to originalStart
    REQUIRE (em.canUndo());
    em.undoLastAction();

    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (originalStart, 0.001));
}

// ---------------------------------------------------------------------------
// Test 3 — AI batch: beginAITransaction + 3 moves -> single undo reverts all 3
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: AI batch transaction collapses to one undo step", "[undo][AUDIO-05]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    // Insert 3 clips side-by-side
    auto* c1 = insertDummyClip (*track, 0.0, 1.0);
    auto* c2 = insertDummyClip (*track, 1.0, 1.0);
    auto* c3 = insertDummyClip (*track, 2.0, 1.0);

    REQUIRE (c1 != nullptr);
    REQUIRE (c2 != nullptr);
    REQUIRE (c3 != nullptr);

    double start1 = c1->getPosition().getStart().inSeconds();
    double start2 = c2->getPosition().getStart().inSeconds();
    double start3 = c3->getPosition().getStart().inSeconds();

    // Begin AI batch — all three moves must collapse into ONE undo step.
    // Calling beginAITransaction() once; NOT calling it again between mutations.
    em.beginAITransaction ("AI Test Batch");

    // Apply mutations directly through clip positions (they go through the
    // shared UndoManager automatically since clips mutate their ValueTree state).
    // Use moveClip() but redirect to setPosition directly after beginAITransaction
    // so they share the same transaction (no new beginNewTransaction in between).
    auto move = [&] (te::Clip* clip, double newStart) {
        double len = clip->getPosition().getLength().inSeconds();
        auto s = TimePos::fromSeconds (newStart);
        auto e = TimePos::fromSeconds (newStart + len);
        clip->setPosition ({ TimeRng (s, e), {} });
    };

    move (c1, 10.0);
    move (c2, 11.0);
    move (c3, 12.0);

    // Verify moves applied
    REQUIRE_THAT (c1->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (10.0, 0.001));
    REQUIRE_THAT (c2->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (11.0, 0.001));
    REQUIRE_THAT (c3->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (12.0, 0.001));

    // ONE undo call should revert ALL three moves (they share one transaction)
    REQUIRE (em.canUndo());
    em.undoLastAction();

    REQUIRE_THAT (c1->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (start1, 0.001));
    REQUIRE_THAT (c2->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (start2, 0.001));
    REQUIRE_THAT (c3->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (start3, 0.001));
}

// ---------------------------------------------------------------------------
// Test 4 — 10 sequential user edits each undo individually
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: 10 sequential user edits are each individually undoable", "[undo][AUDIO-05]")
{
    EditManager em;
    em.createNewEdit();

    auto* track = getFirstAudioTrack (*em.getEdit());
    REQUIRE (track != nullptr);

    auto* clip = insertDummyClip (*track, 0.0, 0.5);
    REQUIRE (clip != nullptr);

    // Record 10 sequential positions (index 0 = initial, 1..9 = after each move)
    std::array<double, 10> positions;
    positions[0] = clip->getPosition().getStart().inSeconds();

    for (int i = 1; i < 10; ++i)
    {
        double newPos = static_cast<double> (i) * 1.0;
        em.moveClip (*clip, newPos);  // each call = one new undo step
        positions[i] = clip->getPosition().getStart().inSeconds();
    }

    // Verify clip is at position 9
    REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                  Catch::Matchers::WithinAbs (9.0, 0.001));

    // Undo each of the 9 moves one at a time: positions[9] -> positions[0]
    for (int i = 9; i >= 1; --i)
    {
        REQUIRE (em.canUndo());
        em.undoLastAction();
        REQUIRE_THAT (clip->getPosition().getStart().inSeconds(),
                      Catch::Matchers::WithinAbs (positions[i - 1], 0.001));
    }
}

// ---------------------------------------------------------------------------
// Test 5 — ValueTree round-trip: saveEdit + loadEdit preserves tracks/clips
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: saveEdit/loadEdit round-trip preserves tracks and clips", "[undo][AUDIO-05]")
{
    auto tempFile = juce::File::createTempFile (".tracktion");

    {
        EditManager em;
        em.createNewEdit();

        auto* track = getFirstAudioTrack (*em.getEdit());
        REQUIRE (track != nullptr);
        insertDummyClip (*track, 3.5, 2.0);

        em.saveEdit (tempFile);
    }

    // Reload in a fresh EditManager
    {
        EditManager em2;
        em2.loadEdit (tempFile);

        auto* edit2 = em2.getEdit();
        REQUIRE (edit2 != nullptr);

        auto tracks2 = te::getAudioTracks (*edit2);
        REQUIRE (tracks2.size() >= 1);

        // At least one clip should exist on the first track
        auto& track2 = *tracks2[0];
        auto clips = track2.getClips();
        REQUIRE (clips.size() >= 1);

        // Verify the clip starts near 3.5
        double loadedStart = clips[0]->getPosition().getStart().inSeconds();
        REQUIRE_THAT (loadedStart, Catch::Matchers::WithinAbs (3.5, 0.001));
    }

    tempFile.deleteFile();
}

// ---------------------------------------------------------------------------
// Test 6 — Edit-swap safety (live-test crash 2026-07-02: reopening a session
// destroyed the old Edit while UI views still referenced it).
//   a) onEditWillChange fires BEFORE the old Edit dies, with the OLD Edit
//      still installed — this is the window where views must detach.
//   b) loadEdit() on an unreadable file returns false and keeps the current
//      Edit fully intact (no crash, no null edit).
// ---------------------------------------------------------------------------
TEST_CASE ("EditManager: edit swap fires onEditWillChange while the old Edit is alive", "[undo][editswap]")
{
    auto tempFile = juce::File::createTempFile (".tracktion");

    EditManager em;
    em.createNewEdit();

    auto* oldEdit = em.getEdit();
    REQUIRE (oldEdit != nullptr);
    em.saveEdit (tempFile);

    bool willChangeFired = false;
    bool oldEditWasAliveAndCurrent = false;
    em.onEditWillChange = [&]
    {
        willChangeFired = true;
        // The OLD edit must still be installed and alive here — views detach now.
        oldEditWasAliveAndCurrent = (em.getEdit() == oldEdit)
                                     && ! te::getAudioTracks (*em.getEdit()).isEmpty();
    };

    bool changedFired = false;
    em.onEditChanged = [&] { changedFired = true; };

    REQUIRE (em.loadEdit (tempFile));

    CHECK (willChangeFired);
    CHECK (oldEditWasAliveAndCurrent);
    CHECK (changedFired);
    CHECK (em.getEdit() != nullptr);
    CHECK (em.getEdit() != oldEdit);

    tempFile.deleteFile();
}

TEST_CASE ("EditManager: loadEdit on an unreadable file fails gracefully and keeps the session", "[undo][editswap]")
{
    EditManager em;
    em.createNewEdit();
    auto* editBefore = em.getEdit();
    REQUIRE (editBefore != nullptr);

    // Garbage file — not a tracktion edit
    auto bogus = juce::File::createTempFile (".tracktion");
    bogus.replaceWithText ("this is not XML at all { ] <<<");

    bool willChangeFired = false;
    em.onEditWillChange = [&] { willChangeFired = true; };

    CHECK_FALSE (em.loadEdit (bogus));
    CHECK_FALSE (willChangeFired);            // nothing was torn down
    CHECK (em.getEdit() == editBefore);       // current session untouched

    bogus.deleteFile();
}

#include "app/UndoMenuObserver.h"

TEST_CASE ("Undo menu refresh follows actions and safely switches sessions", "[undo][undo-menu]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto first = std::make_unique<juce::UndoManager>();
    juce::UndoManager second;
    juce::UndoManager* current = first.get();
    bool canUndo = false, canRedo = false;
    juce::String description;
    int updates = 0;
    UndoMenuObserver observer ([&]
    {
        ++updates;
        canUndo = current != nullptr && current->canUndo();
        canRedo = current != nullptr && current->canRedo();
        description = current != nullptr ? current->getUndoDescription() : juce::String();
    });
    observer.bind (current);
    CHECK_FALSE (canUndo);
    juce::ValueTree state ("TEST");
    first->beginNewTransaction ("Mute track");
    state.setProperty ("mute", true, first.get());
    first->dispatchPendingMessages();
    CHECK (canUndo);
    CHECK_FALSE (canRedo);
    CHECK (description == "Mute track");
    REQUIRE (first->undo());
    first->dispatchPendingMessages();
    CHECK_FALSE (canUndo);
    CHECK (canRedo);
    REQUIRE (first->redo());
    first->dispatchPendingMessages();
    CHECK (canUndo);
    CHECK_FALSE (canRedo);

    // Switch before old-session destruction; even a queued old notification
    // must not refresh the menu for the replacement session.
    state.setProperty ("mute", false, first.get());
    current = &second;
    observer.bind (current);
    CHECK_FALSE (canUndo);
    CHECK_FALSE (canRedo);
    auto count = updates;
    first->dispatchPendingMessages();
    CHECK (updates == count);
    first.reset();
    second.beginNewTransaction ("Add Audio Track");
    state.addChild (juce::ValueTree ("TRACK"), -1, &second);
    second.dispatchPendingMessages();
    CHECK (canUndo);
    CHECK (description == "Add Audio Track");
    observer.detach();
    count = updates;
    second.clearUndoHistory();
    second.dispatchPendingMessages();
    CHECK (updates == count);
}
