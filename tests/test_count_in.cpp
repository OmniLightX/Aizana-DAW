// test_count_in.cpp — Vorzaehler / Pre-Roll (2026-08-16, dritter und richtiger Anlauf).
//
// Geprueft wird die Rechnung, auf der der nahtlose Vorzaehler steht:
// `countin::makePlan` bestimmt, WO die Wiedergabe einsetzt und WO die Aufnahme
// einpuncht. Die beiden Eigenschaften, an denen der Vorgaenger scheiterte, sind
// hier als Test festgeschrieben:
//
//   1. Der Vorlauf beginnt IMMER auf einer Eins (sonst zaehlt der Klick gegen
//      den Song) und ist NIE kuerzer als eingestellt, egal wohin geklickt wurde.
//   2. Am SONGANFANG gibt es trotzdem einen Vorzaehler. Der Vorgaenger klemmte
//      hier mit `jmax (0, ...)` auf Takt 1 und zaehlte dort gar nicht ein —
//      ausgerechnet im haeufigsten Fall (Beatbau ab Takt 1).
//
// Dazu die Tempospur-Treue (T1.2): der Vorlauf muss ueber einen Tempowechsel
// hinweg richtig liegen — die naive Rechnung `Takte × 4 × 60/BPM` ist dort falsch.
//
// Der Transport-Ablauf selbst (einrollen, scharfschalten, Punch, Zustand
// zurueckgeben) braucht ein Audiogeraet und scharfe Eingaenge und ist deshalb
// LIVE-TEST-only — Drehbuch in docs/LIVE_TEST_CHECKLIST.md, Block V.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/CountIn.h"
#include "engine/EditManager.h"
#include "engine/TempoEngine.h"

using BeatPos = tracktion::core::BeatPosition;
using TimePos = tracktion::core::TimePosition;

namespace
{
    /** Taktlaenge in Sekunden bei konstantem Tempo/Taktart. */
    double barSecs (double bpm, int numerator) { return numerator * 60.0 / bpm; }
}

//==============================================================================
TEST_CASE ("Vorzaehler: N Takte vor einem Aufnahmestart auf der Taktlinie", "[countin]")
{
    EditManager em;
    em.createNewEdit();
    auto& ts = em.getEdit()->tempoSequence;

    const double bpm = ts.getTempo (0)->getBpm();
    const int    num = ts.getTimeSig (0)->numerator;
    const double bar = barSecs (bpm, num);

    // Aufnahme soll bei Takt 5 beginnen (Index 4 = fuenfter Takt).
    const auto recStart = ts.toTime (tracktion::tempo::BarsAndBeats { 4 });

    for (int bars : { 1, 2, 4 })
    {
        const auto plan = countin::makePlan (ts, recStart, bars);

        REQUIRE (plan.enabled);
        CHECK (plan.recordStartSecs == Catch::Approx (recStart.inSeconds()));

        // Exakt N Takte, und der Anfang liegt auf der erwarteten Taktlinie.
        CHECK (plan.lengthSecs() == Catch::Approx (bars * bar).margin (1.0e-6));
        CHECK (plan.countInStartSecs
                 == Catch::Approx (ts.toTime (tracktion::tempo::BarsAndBeats { 4 - bars }).inSeconds())
                        .margin (1.0e-6));
    }
}

//==============================================================================
TEST_CASE ("Vorzaehler: krummer Aufnahmestart — Anfang auf der Eins, nie zu kurz", "[countin]")
{
    // Steht der Kopf mitten im Takt (der Normalfall, wenn jemand irgendwo
    // hinklickt), muss der Vorlauf trotzdem auf einer EINS beginnen — sonst
    // zaehlt der Klick gegen das Raster des Songs — und darf dabei nicht kuerzer
    // werden als eingestellt. Beides gleichzeitig geht nur, indem der
    // Aufnahmestart auf seine Taktlinie abgerundet wird; der Vorlauf ist dann
    // etwas laenger. Diese Eigenschaft wird hier festgeschrieben.
    EditManager em;
    em.createNewEdit();
    auto& ts = em.getEdit()->tempoSequence;

    const double bpm = ts.getTempo (0)->getBpm();
    const int    num = ts.getTimeSig (0)->numerator;
    const double bar = barSecs (bpm, num);

    // Mitten im fuenften Takt: Taktlinie 4 plus zwei Beats.
    const auto barLine  = ts.toTime (tracktion::tempo::BarsAndBeats { 4 });
    const auto recStart = TimePos::fromSeconds (barLine.inSeconds() + 2.0 * 60.0 / bpm);

    const auto plan = countin::makePlan (ts, recStart, 2);
    REQUIRE (plan.enabled);

    // Anfang auf der Eins von Takt 3 (Index 2) …
    CHECK (plan.countInStartSecs
             == Catch::Approx (ts.toTime (tracktion::tempo::BarsAndBeats { 2 }).inSeconds())
                    .margin (1.0e-6));

    // … Ende exakt am Aufnahmestart, und die Strecke ist LAENGER als 2 Takte
    // (2 volle Takte + der angebrochene), nicht kuerzer.
    CHECK (plan.recordStartSecs == Catch::Approx (recStart.inSeconds()));
    CHECK (plan.lengthSecs()    >  2.0 * bar);
    CHECK (plan.lengthSecs()    == Catch::Approx (2.0 * bar + 2.0 * 60.0 / bpm).margin (1.0e-6));
}

//==============================================================================
TEST_CASE ("Vorzaehler: 0 Takte und kein Platz liefern keinen Vorlauf", "[countin]")
{
    EditManager em;
    em.createNewEdit();
    auto& ts = em.getEdit()->tempoSequence;

    const auto atBarFive = ts.toTime (tracktion::tempo::BarsAndBeats { 4 });

    CHECK_FALSE (countin::makePlan (ts, atBarFive,  0).enabled);
    CHECK_FALSE (countin::makePlan (ts, atBarFive, -3).enabled);

    // Aufnahmestart exakt am Songanfang: `bars` Takte davor liegen in negativer
    // Zeit. GENAU DAS ist der Fix — der Vorgaenger klemmte hier auf 0 und lieferte
    // damit am Songanfang keinen Vorzaehler. TE spielt und rechnet in negativer
    // Zeit, der Plan ist also gueltig und negativ.
    const auto plan = countin::makePlan (ts, TimePos::fromSeconds (0.0), 2);
    REQUIRE (plan.enabled);
    CHECK (plan.countInStartSecs < 0.0);
    CHECK (plan.recordStartSecs  == Catch::Approx (0.0));
}

//==============================================================================
TEST_CASE ("Vorzaehler: 3/4-Takt — ein Takt sind drei Schlaege, nicht vier", "[countin]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto& ts   = edit.tempoSequence;

    ts.getTimeSig (0)->setStringTimeSig ("3/4");

    const double bpm = ts.getTempo (0)->getBpm();
    const auto recStart = ts.toTime (tracktion::tempo::BarsAndBeats { 4 });

    const auto plan = countin::makePlan (ts, recStart, 2);
    REQUIRE (plan.enabled);
    CHECK (plan.lengthSecs() == Catch::Approx (2.0 * 3.0 * 60.0 / bpm).margin (1.0e-6));
}

//==============================================================================
TEST_CASE ("Vorzaehler: folgt der Tempospur ueber einen Tempowechsel", "[countin]")
{
    // Die naive Rechnung „Takte × Beats × 60/BPM" ist falsch, sobald im Vorlauf
    // ein Tempowechsel liegt (T1.2 Tempo-Track). Hier liegt genau einer drin.
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    auto& ts   = edit.tempoSequence;
    TempoEngine tempo (em);

    const int num = ts.getTimeSig (0)->numerator;   // 4
    REQUIRE (ts.getTempo (0)->getBpm() == Catch::Approx (120.0));

    // Ab Takt 4 (= Beat 12 bei 4/4) laeuft der Song auf 60 BPM.
    const int idx = tempo.addTempoChange (ts.toTime (BeatPos::fromBeats (12.0)).inSeconds());
    REQUIRE (idx > 0);
    tempo.setTempoBpm (idx, 60.0);

    // Aufnahme ab Takt 5 (Beat 16), Vorlauf 2 Takte → ab Takt 3 (Beat 8).
    const auto recStart = ts.toTime (tracktion::tempo::BarsAndBeats { 4 });
    const auto plan     = countin::makePlan (ts, recStart, 2);
    REQUIRE (plan.enabled);

    // Takt 3 laeuft noch auf 120 BPM (2,0 s), Takt 4 schon auf 60 BPM (4,0 s).
    CHECK (plan.lengthSecs() == Catch::Approx (2.0 + 4.0).margin (1.0e-6));

    // Und ausdruecklich NICHT die naive Rechnung mit dem Grundtempo:
    CHECK (plan.lengthSecs() != Catch::Approx (2.0 * num * 60.0 / 120.0).margin (1.0e-3));
}

//==============================================================================
TEST_CASE ("Vorzaehler: Aufnahmestart wird nie verschoben", "[countin]")
{
    // Ein Vorzaehler darf den Aufnahmepunkt NICHT anfassen — er haengt vorne
    // Zeit an, mehr nicht. (Der Punkt selbst wird spaeter ueber TEs Punch-Bereich
    // sample-genau gehalten; hier wird festgeschrieben, dass die Rechnung ihn
    // unveraendert durchreicht, auch an krummen Stellen.)
    EditManager em;
    em.createNewEdit();
    auto& ts = em.getEdit()->tempoSequence;

    for (double t : { 0.0, 0.37, 1.0, 3.1415, 12.5, 61.75 })
        for (int bars : { 1, 2, 4 })
        {
            const auto plan = countin::makePlan (ts, TimePos::fromSeconds (t), bars);
            if (plan.enabled)
                CHECK (plan.recordStartSecs == Catch::Approx (t));
        }
}
