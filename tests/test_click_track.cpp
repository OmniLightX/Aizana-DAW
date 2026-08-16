// test_click_track.cpp — Das Metronom ist ab 2026-08-16 TEs Klickspur.
//
// WARUM DIESER TEST EXISTIERT
// ---------------------------
// Aizana hatte bis dahin einen EIGENEN Klickgenerator, der frei lief: er zaehlte
// Beats ab dem Einschalten, unabhaengig vom Abspielkopf. Er klackte im
// Stillstand, seine Eins lag nicht auf der Eins des Songs, und Tempowechsel
// kannte er nicht. Ein Vorzaehler ist darauf nicht baubar — deshalb ist er
// ersetzt worden.
//
// Genau dieser Tausch ist die gefaehrliche Sorte Aenderung: ein BELEGTER Weg
// (man hat den alten Klick gehoert) wird gegen einen NEUEN getauscht, den
// niemand unter denselben Bedingungen belegt hat. Genau daran ist in dieser
// Sitzung schon zweimal der Ton verloren gegangen. Also wird der neue Weg hier
// belegt, bevor der Anwender ihn hoert:
//
//   1. Klick AN  → im Ausgang liegt Energie, und zwar auf den Beats.
//   2. Klick AUS → Stille. (Sonst „klackt es die ganze Zeit".)
//   3. Klick AUS + Klickbereich gesetzt → NUR in diesem Bereich Energie.
//      Das ist der Vorzaehler: zaehlen, dann Ruhe.
//   4. Die Eins ist lauter/anders als die uebrigen Schlaege (Betonung).
//
// Geprueft wird TEs `ClickGenerator` direkt — dieselbe Klasse, die der
// `ClickNode` im Render-Graphen benutzt.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"

// ClickGenerator ist die Klasse, die TEs ClickNode im Render-Graphen benutzt.
// Ihr Header wird von TE nur in die Modul-.cpp gezogen, nicht in den oeffentlichen
// Sammel-Header — fuer den Nachweis holen wir ihn hier direkt.
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_ClickNode.h>

using TimePos = tracktion::core::TimePosition;

namespace
{
    constexpr double kSampleRate = 44100.0;

    /** Ergebnis eines Klick-Renders ueber einen Zeitbereich. */
    struct ClickRender
    {
        float peak   = 0.0f;
        int   onsets = 0;                 // Anzahl getrennter Klick-Ereignisse
        std::vector<float> samples;       // fuer Vergleiche (Betonung)
    };

    /** Rendert den Klick blockweise fuer [from, to).

        Die Ereigniszaehlung braucht eine HALTEZEIT: ein Klick ist ein
        abklingender Ton, der mehrfach durch die Null geht. Ohne Haltezeit zaehlt
        man Nulldurchgaenge statt Klicks — der erste Anlauf dieses Tests meldete
        dadurch 9 statt 4. Ein neues Ereignis gilt erst, wenn davor mindestens
        eine halbe Klicklaenge (≈ 2 ms) durchgehend Ruhe lag. */
    ClickRender renderClick (te::Edit& edit, double fromSecs, double toSecs)
    {
        te::ClickGenerator gen (edit, false);
        gen.prepareToPlay (kSampleRate, TimePos::fromSeconds (fromSecs));

        const int    blockSamples = 512;
        const double blockSecs    = blockSamples / kSampleRate;
        const int    holdSamples  = (int) (kSampleRate * 0.002);

        std::vector<float> left ((size_t) blockSamples), right ((size_t) blockSamples);
        float* channels[2] = { left.data(), right.data() };

        ClickRender out;
        int quietRun = holdSamples;       // Start gilt als "vorher war Ruhe"

        for (double t = fromSecs; t < toSecs - 1.0e-9; t += blockSecs)
        {
            std::fill (left.begin(),  left.end(),  0.0f);
            std::fill (right.begin(), right.end(), 0.0f);

            auto view = choc::buffer::createChannelArrayView (channels, 2u,
                                                              (choc::buffer::FrameCount) blockSamples);

            // Letzten Block auf das Bereichsende KLEMMEN. Ohne das ragt der
            // Block ueber `toSecs` hinaus und nimmt den Schlag, der GENAU auf der
            // Bereichsgrenze liegt, noch mit — der erste Anlauf zaehlte deshalb
            // 5 statt 4 Schlaege und meldete Klick vor dem Vorzaehler.
            const double blockEnd = std::min (t + blockSecs, toSecs);

            gen.processBlock (&view, nullptr,
                              tracktion::TimeRange (TimePos::fromSeconds (t),
                                                    TimePos::fromSeconds (blockEnd)));

            for (int i = 0; i < blockSamples; ++i)
            {
                const float v   = left[(size_t) i];
                const float mag = std::abs (v);

                out.samples.push_back (v);
                out.peak = std::max (out.peak, mag);

                if (mag > 0.01f)
                {
                    if (quietRun >= holdSamples)
                        ++out.onsets;

                    quietRun = 0;
                }
                else
                {
                    ++quietRun;
                }
            }
        }

        return out;
    }
}

//==============================================================================
TEST_CASE ("Klickspur: eingeschaltet klingt sie, ausgeschaltet ist Stille", "[click][countin]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();

    // ClickGenerator setzt einen Wiedergabekontext voraus (EditManager legt ihn
    // beim Anlegen an). Ohne Audiogeraet gibt es keinen — dann ist hier nichts
    // zu messen, und das MUSS auffallen statt still durchzugehen.
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    // 120 BPM, 4/4 → vier Schlaege in 2 Sekunden.
    REQUIRE (edit.tempoSequence.getTempo (0)->getBpm() == Catch::Approx (120.0));

    edit.clickTrackEnabled = true;
    const auto on = renderClick (edit, 0.0, 2.0);

    CHECK (on.peak   > 0.05f);      // es kommt wirklich Ton
    CHECK (on.onsets == 4);         // und zwar genau vier Schlaege in zwei Sekunden

    edit.clickTrackEnabled = false;
    const auto off = renderClick (edit, 0.0, 2.0);

    CHECK (off.peak   == Catch::Approx (0.0f).margin (1.0e-7));
    CHECK (off.onsets == 0);
}

//==============================================================================
TEST_CASE ("Klickspur: Klickbereich = Vorzaehler — nur dort klingt es", "[click][countin]")
{
    // DAS IST DER VORZAEHLER. Der Klick des Anwenders steht auf AUS; fuer die
    // Dauer des Vorlaufs setzt CountInRecorder einen Klickbereich, und danach
    // ist wieder Ruhe. Logics „Only during count-in".
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    edit.clickTrackEnabled = false;

    // Vorzaehler: zwei Takte (4 s bei 120 BPM 4/4) vor einem Aufnahmestart bei
    // 8 s. Das Ende liegt eine HALBE Zaehlzeit davor (7.75 s) — genau so setzt
    // CountInRecorder den Bereich, und genau so macht es TE in seinem eigenen
    // Vorzaehler: der Schlag AUF dem Aufnahmestart soll nicht mehr klingen
    // („1-2-3-4" und dann Aufnahme, nicht „1-2-3-4-5").
    edit.setClickTrackRange ({ TimePos::fromSeconds (4.0), TimePos::fromSeconds (7.75) });

    const auto duringRunUp = renderClick (edit, 4.0, 7.75);
    const auto beforeRunUp = renderClick (edit, 0.0, 4.0);
    const auto afterRecord = renderClick (edit, 7.75, 12.0);

    CHECK (duringRunUp.onsets == 8);      // acht Schlaege = zwei Takte gezaehlt
    CHECK (duringRunUp.peak    > 0.05f);

    CHECK (beforeRunUp.peak == Catch::Approx (0.0f).margin (1.0e-7));
    CHECK (afterRecord.peak == Catch::Approx (0.0f).margin (1.0e-7));

    // Und ohne Bereich ist der ausgeschaltete Klick ueberall still — der Bereich
    // ist also wirklich die Ursache und nicht ein Nebeneffekt.
    edit.setClickTrackRange ({});
    CHECK (renderClick (edit, 4.0, 7.75).peak == Catch::Approx (0.0f).margin (1.0e-7));
}

//==============================================================================
TEST_CASE ("Klickspur: die Eins ist betont — und die Betonung ist abschaltbar", "[click]")
{
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    edit.clickTrackEnabled       = true;
    edit.clickTrackEmphasiseBars = true;

    // Erster Schlag des Taktes (0.0 s) gegen den zweiten (0.5 s). Verglichen wird
    // der KLANG, nicht der Spitzenpegel: TE nimmt zwei verschiedene Klick-Samples
    // (bigclick/littleclick), die durchaus gleich laut sein duerfen.
    const auto downbeat = renderClick (edit, 0.0, 0.4);
    const auto offbeat  = renderClick (edit, 0.5, 0.9);

    REQUIRE (downbeat.onsets == 1);
    REQUIRE (offbeat.onsets  == 1);
    REQUIRE (downbeat.samples.size() == offbeat.samples.size());
    CHECK (downbeat.samples != offbeat.samples);

    edit.clickTrackEmphasiseBars = false;
    const auto flatDown = renderClick (edit, 0.0, 0.4);
    const auto flatOff  = renderClick (edit, 0.5, 0.9);
    CHECK (flatDown.samples == flatOff.samples);      // ohne Betonung identisch
}

//==============================================================================
TEST_CASE ("Klickspur: folgt der Tempospur", "[click]")
{
    // Der abgeloeste Generator kannte nur EINEN BPM-Wert. TEs Klickspur liest die
    // Tempospur — bei 60 BPM sind in zwei Sekunden zwei Schlaege, nicht vier.
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    edit.clickTrackEnabled = true;
    CHECK (renderClick (edit, 0.0, 2.0).onsets == 4);       // 120 BPM

    edit.tempoSequence.getTempo (0)->setBpm (60.0);
    CHECK (renderClick (edit, 0.0, 2.0).onsets == 2);       // 60 BPM
}
