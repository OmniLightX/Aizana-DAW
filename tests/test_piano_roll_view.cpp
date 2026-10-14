#include <catch2/catch_test_macros.hpp>
#include "ui/pianoroll/PianoRollView.h"
#include "engine/InstrumentTracks.h"

TEST_CASE ("Piano roll opens recorded pitches inside the note grid and scrolls independently", "[pianoroll][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto* track = aizana::createInstrumentTrack (*em.getEdit(), "Piano test", [] (te::AudioTrack&) {});
    auto* clip = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
    REQUIRE (clip != nullptr);
    for (int p : { 60, 64, 67, 72 })
        clip->getSequence().addNote (p, tracktion::core::BeatPosition::fromBeats (2),
            tracktion::core::BeatDuration::fromBeats (1), 100, 0, nullptr);
    PianoRollView view (*clip, em);
    view.setSize (1000, 600);
    for (int p : { 60, 64, 67, 72 })
    {
        CHECK (view.yAtPitch (p) >= 64);
        CHECK (view.yAtPitch (p) + view.rowHeightPx() < 550);
        CHECK (view.pitchAtY (view.yAtPitch (p) + float (view.rowHeightPx() * 0.5)) == p);
    }
    auto* vertical = static_cast<juce::ScrollBar*> (nullptr);
    for (auto* child : view.getChildren())
        if (auto* bar = dynamic_cast<juce::ScrollBar*> (child); bar && bar->isVertical()) vertical = bar;
    REQUIRE (vertical != nullptr);
    const auto before = view.yAtPitch (64);
    const auto start = vertical->getCurrentRangeStart();
    view.setHorizontalZoom (120);
    CHECK (vertical->getCurrentRangeStart() == start);
    CHECK (view.yAtPitch (64) == before);
    vertical->setCurrentRangeStart (start + view.rowHeightPx(), juce::sendNotificationSync);
    CHECK (view.yAtPitch (64) < before); // moving scrollbar down reveals lower pitches
}

#include "ui/arrangement/MidiClipPreview.h"
#include <catch2/matchers/catch_matchers_floating_point.hpp>
using Catch::Matchers::WithinAbs;

TEST_CASE ("MIDI preview follows trimmed and looped content without altering notes", "[pianoroll][preview]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto* track = aizana::createInstrumentTrack (*em.getEdit(), "Preview", [] (te::AudioTrack&) {});
    auto* clip = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
    REQUIRE (clip);
    using namespace tracktion::core;
    clip->getSequence().addNote (64, BeatPosition::fromBeats (2), BeatDuration::fromBeats (1), 90, 0, nullptr);
    clip->setPosition ({ TimeRange (TimePosition::fromSeconds (10), TimePosition::fromSeconds (14)), TimeDuration::fromSeconds (0.5) });
    auto notes = aizana::midiClipPreview (*clip);
    REQUIRE (notes.size() == 1);
    CHECK_THAT (notes[0].start, WithinAbs (0.5, 0.0001));
    CHECK_THAT (notes[0].end, WithinAbs (1.0, 0.0001));
    CHECK (notes[0].pitch == 64);
    clip->setOffset (TimeDuration());
    clip->setLoopRangeBeats ({ BeatPosition::fromBeats (2), BeatPosition::fromBeats (4) });
    notes = aizana::midiClipPreview (*clip);
    REQUIRE (notes.size() == 4);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        CHECK_THAT (notes[i].start, WithinAbs ((double) i, 0.0001));
        CHECK_THAT (notes[i].end, WithinAbs ((double) i + 0.5, 0.0001));
    }
    CHECK (clip->getSequence().getNumNotes() == 1);
    CHECK_THAT (clip->getSequence().getNote (0)->getStartBeat().inBeats(), WithinAbs (2, 1e-9));
}

TEST_CASE ("Piano roll fits extreme pitches and clips note children to the grid", "[pianoroll][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto* track = aizana::createInstrumentTrack (*em.getEdit(), "Extremes", [] (te::AudioTrack&) {});
    auto* clip = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
    REQUIRE (clip);
    for (int p : {0, 60, 127})
        clip->getSequence().addNote (p, tracktion::core::BeatPosition::fromBeats (2),
            tracktion::core::BeatDuration::fromBeats (1), 100, 0, nullptr);
    PianoRollView view (*clip, em);
    view.setSize (1000, 600);
    for (auto* child : view.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b && b->getTitle() == "Alle Noten anzeigen") b->onClick();
    for (int p : {0,60,127})
    {
        CHECK (view.yAtPitch (p) >= 64);
        CHECK (view.yAtPitch (p) + view.rowHeightPx() <= 540.1);
    }
    juce::Component* layer = nullptr;
    for (auto* child : view.getChildren())
        if (child->getNumChildComponents() == 3 && dynamic_cast<NoteComponent*> (child->getChildComponent (0))) layer = child;
    REQUIRE (layer);
    CHECK (layer->getY() == 64);
    CHECK (layer->getBottom() <= 540);
    view.setVerticalZoom (30);
    for (auto* child : layer->getChildren())
        CHECK (child->getParentComponent() == layer);
    // Deferred updates must not retain a destroyed editor.
    clip->getSequence().removeNote (*clip->getSequence().getNote (0), &em.getUndoManager());
    CHECK (view.isNoteRebuildPending());
    view.setHorizontalZoom (120);
    view.setSize (1100, 610);
    view.setKey (0, {0,2,4,5,7,9,11});
    view.keyPressed (juce::KeyPress ('q'));
    view.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (clip->getSequence().getNumNotes() == 2);
}

TEST_CASE ("Piano roll starts readable despite outlier pitches", "[pianoroll][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto* track = aizana::createInstrumentTrack (*em.getEdit(), "Readable", [] (te::AudioTrack&) {});
    auto* clip = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
    REQUIRE (clip);
    for (int p : {0, 60, 60, 64, 64, 67, 67, 98})
        clip->getSequence().addNote (p, tracktion::core::BeatPosition::fromBeats (2),
            tracktion::core::BeatDuration::fromBeats (1), 100, 0, nullptr);
    PianoRollView view (*clip, em);
    view.setSize (1000, 400);
    CHECK (view.rowHeightPx() >= 12.0);
    CHECK (view.yAtPitch (64) >= 64);
    CHECK (view.yAtPitch (64) + view.rowHeightPx() < 340);
    CHECK (clip->getSequence().getNumNotes() == 8);
}

TEST_CASE ("Controller lane is optional and preserves usable editing height", "[pianoroll][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto* track = aizana::createInstrumentTrack (*em.getEdit(), "Layout", [] (te::AudioTrack&) {});
    auto* clip = dynamic_cast<te::MidiClip*> (track->getClips()[0]);
    REQUIRE (clip);
    PianoRollView view (*clip, em);
    view.setSize (1000, 320);
    juce::Component* grid = nullptr;
    juce::TextButton* cc = nullptr;
    for (auto* child : view.getChildren())
    {
        if (child->getTitle() == "Notenraster") grid = child;
        if (auto* b = dynamic_cast<juce::TextButton*> (child); b && b->getTitle() == "Controller-Spur anzeigen") cc = b;
    }
    // Notenraster is the parent view; use the clipped note layer dimensions via children.
    for (auto* child : view.getChildren())
        if (child->getX() == 40 && child->getY() == 64 && child->getHeight() > 100) grid = child;
    REQUIRE (grid);
    REQUIRE (cc);
    auto height = grid->getHeight();
    CHECK (height >= 200);
    cc->onClick();
    CHECK (cc->getToggleState());
    CHECK (grid->getHeight() == height - 60);
    CHECK (grid->getHeight() >= 140);
    cc->onClick();
    CHECK (grid->getHeight() == height);
}
