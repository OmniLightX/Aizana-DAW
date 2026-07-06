// test_quantize.cpp — E7: Quantize-Bar (MIDI zuerst).
//
// (1) Golden-Tests der puren Quantize-Mathematik ClipEditor::quantizedBeat
//     (Grid/Strength/Swing — kein TE nötig).
// (2) Regressionstest quantizeMidiClip auf einem echten MidiClip: Noten landen
//     auf dem Raster, EIN Undo-Schritt stellt alle Startzeiten wieder her.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using BeatPos = tracktion::core::BeatPosition;
using BeatDur = tracktion::core::BeatDuration;

namespace
{
    te::MidiClip* insertMidiClip (te::AudioTrack& track, double startSecs, double lengthSecs)
    {
        te::ClipPosition pos { TimeRng (TimePos::fromSeconds (startSecs),
                                        TimePos::fromSeconds (startSecs + lengthSecs)), {} };
        return dynamic_cast<te::MidiClip*> (
            track.insertNewClip (te::TrackItem::Type::midi, "q", pos, nullptr));
    }
}

TEST_CASE ("E7: quantizedBeat — pure grid/strength/swing math", "[quantize][e7]")
{
    // Volle Stärke, kein Swing: aufs nächste 1/16 (0.25 Beats).
    CHECK (ClipEditor::quantizedBeat (0.30, 0.25, 1.0, 0.0) == Catch::Approx (0.25));
    CHECK (ClipEditor::quantizedBeat (0.40, 0.25, 1.0, 0.0) == Catch::Approx (0.50));
    CHECK (ClipEditor::quantizedBeat (1.00, 0.25, 1.0, 0.0) == Catch::Approx (1.00));
    CHECK (ClipEditor::quantizedBeat (0.125, 0.25, 1.0, 0.0) == Catch::Approx (0.25));  // Mitte → round up

    // Strength 50 %: halber Weg zum Ziel (S1 "natürlicher").
    CHECK (ClipEditor::quantizedBeat (0.30, 0.25, 0.5, 0.0) == Catch::Approx (0.275));
    // Strength 0: unverändert.
    CHECK (ClipEditor::quantizedBeat (0.30, 0.25, 0.0, 0.0) == Catch::Approx (0.30));

    // Swing 100 %: ungerade Rasterlinien liegen um grid/2 später —
    // Linie 1 (0.25) wird zu 0.375; eine Note bei 0.30 zieht dorthin.
    CHECK (ClipEditor::quantizedBeat (0.30, 0.25, 1.0, 1.0) == Catch::Approx (0.375));
    // Gerade Linien bleiben exakt: 0.55 → Linie 2 (0.50).
    CHECK (ClipEditor::quantizedBeat (0.55, 0.25, 1.0, 1.0) == Catch::Approx (0.50));

    // Grid 0 = no-op (Guard).
    CHECK (ClipEditor::quantizedBeat (0.42, 0.0, 1.0, 0.0) == Catch::Approx (0.42));
}

TEST_CASE ("E7: quantizeMidiClip — Noten aufs Raster, 1 Undo stellt alles wieder her", "[quantize][e7]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    REQUIRE (track != nullptr);

    auto* clip = insertMidiClip (*track, 0.0, 8.0);
    REQUIRE (clip != nullptr);

    // Drei "menschlich" verschobene Noten + eine bereits saubere.
    auto& seq = clip->getSequence();
    auto& um  = em.getUndoManager();
    seq.addNote (60, BeatPos::fromBeats (0.31), BeatDur::fromBeats (0.5), 100, 0, &um);
    seq.addNote (62, BeatPos::fromBeats (1.13), BeatDur::fromBeats (0.5), 100, 0, &um);
    seq.addNote (64, BeatPos::fromBeats (2.48), BeatDur::fromBeats (0.5), 100, 0, &um);
    seq.addNote (65, BeatPos::fromBeats (3.00), BeatDur::fromBeats (0.5), 100, 0, &um);

    ClipEditor ce (em);
    const int moved = ce.quantizeMidiClip (*clip, 0.25, 1.0, 0.0);
    CHECK (moved == 3);   // die saubere Note (3.00) bewegt sich nicht

    juce::Array<double> starts;
    for (auto* n : seq.getNotes())
        starts.add (n->getStartBeat().inBeats());

    REQUIRE (starts.size() == 4);
    CHECK (starts[0] == Catch::Approx (0.25));
    CHECK (starts[1] == Catch::Approx (1.25));   // 1.13: Linie 5 (1.25) liegt näher als Linie 4 (1.00)
    CHECK (starts[2] == Catch::Approx (2.50));
    CHECK (starts[3] == Catch::Approx (3.00));

    // EIN Undo — alle Startzeiten zurück.
    em.getUndoManager().undo();
    juce::Array<double> restored;
    for (auto* n : seq.getNotes())
        restored.add (n->getStartBeat().inBeats());

    REQUIRE (restored.size() == 4);
    CHECK (restored[0] == Catch::Approx (0.31));
    CHECK (restored[1] == Catch::Approx (1.13));
    CHECK (restored[2] == Catch::Approx (2.48));
    CHECK (restored[3] == Catch::Approx (3.00));
}

TEST_CASE ("E7: quantizeMidiClip — Subset (Selektion) lässt andere Noten unberührt", "[quantize][e7]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit())[0];
    auto* clip  = insertMidiClip (*track, 0.0, 8.0);
    REQUIRE (clip != nullptr);

    auto& seq = clip->getSequence();
    auto& um  = em.getUndoManager();
    seq.addNote (60, BeatPos::fromBeats (0.31), BeatDur::fromBeats (0.5), 100, 0, &um);
    seq.addNote (62, BeatPos::fromBeats (1.13), BeatDur::fromBeats (0.5), 100, 0, &um);

    juce::Array<te::MidiNote*> subset;
    subset.add (seq.getNotes()[0]);

    ClipEditor ce (em);
    CHECK (ce.quantizeMidiClip (*clip, 0.25, 1.0, 0.0, &subset) == 1);

    CHECK (seq.getNotes()[0]->getStartBeat().inBeats() == Catch::Approx (0.25));
    CHECK (seq.getNotes()[1]->getStartBeat().inBeats() == Catch::Approx (1.13));   // unangetastet
}
