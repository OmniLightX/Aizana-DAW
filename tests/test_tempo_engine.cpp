// test_tempo_engine.cpp — T1.2: Tempo-Track (Tempo-/Taktartwechsel auf TE TempoSequence).
//
// Deckt: Einfügen (Beat-gerundet, übernimmt geltendes Tempo), BPM setzen (Clamp),
// Löschen (Punkt 0 geschützt), Taktartwechsel, Undo pro Geste, und dass die
// Zeitachse nach einem Tempowechsel wirklich anders läuft (Beat→Zeit-Mapping).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/TempoEngine.h"

TEST_CASE ("T1.2: addTempoChange fügt Beat-gerundet ein und übernimmt das geltende Tempo", "[tempo][t12]")
{
    EditManager em;
    em.createNewEdit();
    TempoEngine tempo (em);

    auto base = tempo.getTempoPoints();
    REQUIRE (base.size() == 1);            // Song-Grundtempo
    const double baseBpm = base[0].bpm;

    // Bei 120 BPM ist Beat 8 = 4.0 s. 4.1 s → rundet auf Beat 8.
    const int idx = tempo.addTempoChange (4.1);
    REQUIRE (idx > 0);

    auto points = tempo.getTempoPoints();
    REQUIRE (points.size() == 2);
    CHECK (points[idx].beat == Catch::Approx (std::round (points[idx].beat)));   // ganzer Beat
    CHECK (points[idx].bpm  == Catch::Approx (baseBpm));                          // Tempo übernommen

    // Undo entfernt den Wechsel wieder (eigene Transaktion).
    em.getUndoManager().undo();
    CHECK (tempo.getTempoPoints().size() == 1);
}

TEST_CASE ("T1.2: setTempoBpm clampt und wirkt aufs Zeit-Mapping", "[tempo][t12]")
{
    EditManager em;
    em.createNewEdit();
    TempoEngine tempo (em);
    auto& seq = em.getEdit()->tempoSequence;

    const int idx = tempo.addTempoChange (4.0);
    REQUIRE (idx > 0);

    tempo.setTempoBpm (idx, 60.0);
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (60.0));

    // Ab dem Wechselpunkt läuft 1 Beat = 1 s (60 BPM) statt 0.5 s (120 BPM):
    // Beat 8 = 4.0 s (120 BPM davor), Beat 12 = 4.0 + 4×1.0 = 8.0 s.
    using BeatPos = tracktion::core::BeatPosition;
    CHECK (seq.toTime (BeatPos::fromBeats (12.0)).inSeconds() == Catch::Approx (8.0).margin (0.01));

    // Clamp: absurde Werte landen in TEs [20..300].
    tempo.setTempoBpm (idx, 9999.0);
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (300.0));   // TE maxBPM
    tempo.setTempoBpm (idx, 1.0);
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (20.0));
}

TEST_CASE ("T1.2: removeTempoChange schützt Punkt 0; Drag-Geste = 1 Undo", "[tempo][t12]")
{
    EditManager em;
    em.createNewEdit();
    TempoEngine tempo (em);

    tempo.removeTempoChange (0);                      // muss No-op sein
    CHECK (tempo.getTempoPoints().size() == 1);

    const int idx = tempo.addTempoChange (2.0, 90.0);
    REQUIRE (idx > 0);
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (90.0));

    // Drag-Geste: erste Bewegung öffnet die Transaktion, weitere falten hinein.
    tempo.setTempoBpm (idx, 100.0, true);
    tempo.setTempoBpm (idx, 110.0, false);
    tempo.setTempoBpm (idx, 121.0, false);
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (121.0));

    em.getUndoManager().undo();                       // EIN Undo → zurück auf 90
    CHECK (tempo.getTempoPoints()[idx].bpm == Catch::Approx (90.0));

    tempo.removeTempoChange (idx);
    CHECK (tempo.getTempoPoints().size() == 1);
}

TEST_CASE ("T1.2: Taktartwechsel einfügen/ändern/löschen (Index 0 geschützt)", "[tempo][t12]")
{
    EditManager em;
    em.createNewEdit();
    TempoEngine tempo (em);

    auto base = tempo.getTimeSigPoints();
    REQUIRE (base.size() == 1);
    CHECK (base[0].numerator == 4);

    const int idx = tempo.addTimeSigChange (8.0, 3, 4);
    REQUIRE (idx > 0);

    auto sigs = tempo.getTimeSigPoints();
    REQUIRE (sigs.size() == 2);
    CHECK (sigs[idx].numerator   == 3);
    CHECK (sigs[idx].denominator == 4);

    tempo.setTimeSig (idx, 7, 8);
    sigs = tempo.getTimeSigPoints();
    CHECK (sigs[idx].numerator   == 7);
    CHECK (sigs[idx].denominator == 8);

    tempo.removeTimeSigChange (0);                    // geschützt
    CHECK (tempo.getTimeSigPoints().size() == 2);

    tempo.removeTimeSigChange (idx);
    CHECK (tempo.getTimeSigPoints().size() == 1);
}
