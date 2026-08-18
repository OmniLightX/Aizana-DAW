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

//==============================================================================
// AB HIER: der Befund vom 2026-08-17 — „der Klick hoert beim Einpunchen nicht auf".
//
// Der Vorzaehler war nicht schuld. Schuld war eine fehlende UNTERSCHEIDUNG:
// `ClickGenerator::isMutedAtTime()` wertet den Klickbereich NUR aus, solange
// `clickTrackEnabled` AUS ist:
//
//     if (! clickEnabled) return ! range.contains (time);
//     return ! clickEnabled;              // <- Schalter an: Bereich egal
//
// Wer das Metronom eingeschaltet hatte (und das tut jeder, der es beim
// Beatbau braucht), hoerte es also durch die ganze Aufnahme — der Vorzaehler
// setzte brav seinen Bereich, und der Bereich tat nichts.
//
// Logic Pro und Studio One trennen das ausdruecklich:
//   * Logic: „Click while playing" / „Click while recording" / darunter
//     „Only during count-in".
//   * Studio One: „Click in play" / „Click in precount only".
// Genau diese Trennung ist jetzt `countin::decideClick` — und sie wird hier
// erst als Tabelle und dann AM KLANG belegt.

#include "engine/CountIn.h"

using TimeRng = tracktion::TimeRange;

TEST_CASE ("Klick bei Aufnahme: die Entscheidungstabelle", "[click][countin][recclick]")
{
    const TimeRng runUp { TimePos::fromSeconds (4.0), TimePos::fromSeconds (7.75) };

    SECTION ("Metronom AN + Klick bei Aufnahme AN = laeuft durch (Beatbau)")
    {
        const auto s = countin::decideClick (true, true, true, runUp);
        CHECK (s.enabled);
        CHECK (s.range.isEmpty());        // Bereich waere hier ohnehin wirkungslos
    }

    SECTION ("Metronom AN + Klick bei Aufnahme AUS = zaehlt vor, dann Ruhe (Gesang)")
    {
        // GENAU DIESER FALL WAR KAPUTT: vorher blieb der Schalter an und der
        // Bereich wirkungslos — es klackte durch die ganze Aufnahme.
        const auto s = countin::decideClick (true, true, false, runUp);
        CHECK_FALSE (s.enabled);
        CHECK (s.range == runUp);
    }

    SECTION ("Metronom AUS + Vorzaehler = nur der Vorzaehler klingt")
    {
        const auto s = countin::decideClick (false, true, true, runUp);
        CHECK_FALSE (s.enabled);
        CHECK (s.range == runUp);
    }

    SECTION ("Metronom AUS + kein Vorzaehler (Pre-Roll) = durchgehend still")
    {
        const auto s = countin::decideClick (false, false, true, runUp);
        CHECK_FALSE (s.enabled);
        CHECK (s.range.isEmpty());
    }

    SECTION ("Metronom AN + Pre-Roll + Klick bei Aufnahme AUS = Anlauf klingt, Aufnahme nicht")
    {
        // Ohne Vorzaehler, aber mit eingeschaltetem Metronom ist der Vorlauf
        // schlicht Wiedergabe — die muss klingen. Erst ab dem Aufnahmestart
        // ist Ruhe.
        const auto s = countin::decideClick (true, false, false, runUp);
        CHECK_FALSE (s.enabled);
        CHECK (s.range == runUp);
    }

    SECTION ("Aufnahme ohne jeden Vorlauf: Klick bei Aufnahme AUS = sofort still")
    {
        const auto s = countin::decideClick (true, false, false, {});
        CHECK_FALSE (s.enabled);
        CHECK (s.range.isEmpty());        // leerer Bereich = ueberall stumm
    }
}

//==============================================================================
TEST_CASE ("Klick bei Aufnahme AUS: mit EINGESCHALTETEM Metronom hoert er am Aufnahmestart auf",
           "[click][countin][recclick]")
{
    // Der Klangbeleg zur Tabelle — und die Regression, die den Befund gefangen
    // haette. Ausgangslage ist die des Anwenders: das Metronom ist AN.
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    edit.clickTrackEnabled = true;                       // wie beim Anwender

    const TimeRng runUp { TimePos::fromSeconds (4.0), TimePos::fromSeconds (7.75) };
    const auto setup = countin::decideClick (edit.clickTrackEnabled.get(), true, false, runUp);

    edit.clickTrackEnabled = setup.enabled;
    edit.setClickTrackRange (setup.range);

    const auto duringRunUp = renderClick (edit, 4.0, 7.75);
    const auto whileRecord = renderClick (edit, 8.0, 14.0);

    CHECK (duringRunUp.onsets == 8);      // zwei Takte werden vorgezaehlt
    CHECK (duringRunUp.peak    > 0.05f);
    CHECK (whileRecord.peak   == Catch::Approx (0.0f).margin (1.0e-7));   // dann RUHE

    // Gegenprobe „Beatbau": derselbe Vorzaehler mit „Klick bei Aufnahme AN"
    // muss durchlaufen — sonst haetten wir nur den einen Fall gegen den anderen
    // getauscht.
    const auto through = countin::decideClick (true, true, true, runUp);
    edit.clickTrackEnabled = through.enabled;
    edit.setClickTrackRange (through.range);

    CHECK (renderClick (edit, 8.0, 14.0).onsets == 12);   // 6 s bei 120 BPM
}

//==============================================================================
TEST_CASE ("Klickspur: die Schlaege liegen auf den Taktlinien — auch mitten im Song (V3/V4)",
           "[click]")
{
    // V3 aus der Testliste, maschinell: der abgeloeste Generator zaehlte ab dem
    // EINSCHALTEN — startete man bei Takt 9, lag seine Eins irgendwo. Gemessen
    // wird deshalb nicht „es klackt", sondern WO es klackt.
    EditManager em;
    em.createNewEdit();
    auto& edit = *em.getEdit();
    REQUIRE (edit.getTransport().getCurrentPlaybackContext() != nullptr);

    edit.clickTrackEnabled       = true;
    edit.clickTrackEmphasiseBars = true;

    auto& ts = edit.tempoSequence;

    auto onsetTimes = [&] (double fromSecs, double toSecs)
    {
        const auto r = renderClick (edit, fromSecs, toSecs);
        std::vector<double> times;
        const int holdSamples = (int) (kSampleRate * 0.002);
        int quiet = holdSamples;

        for (size_t i = 0; i < r.samples.size(); ++i)
        {
            if (std::abs (r.samples[i]) > 0.01f)
            {
                if (quiet >= holdSamples)
                    times.push_back (fromSecs + (double) i / kSampleRate);

                quiet = 0;
            }
            else
                ++quiet;
        }

        return times;
    };

    for (double bpm : { 120.0, 90.0 })          // V4: auch nach Tempowechsel
    {
        ts.getTempo (0)->setBpm (bpm);

        // Takt 9 = Taktindex 8. Zwei Takte lang messen.
        const auto barNine = ts.toTime (tracktion::tempo::BarsAndBeats { 8 });
        const auto barLen  = ts.toTime (tracktion::tempo::BarsAndBeats { 9 }) - barNine;
        const double from  = barNine.inSeconds();

        // Eine halbe Zaehlzeit VOR der naechsten Taktlinie enden. Sonst faellt
        // der Schlag, der genau auf der Bereichsgrenze liegt, mal rein und mal
        // nicht (Gleitkomma) — der erste Lauf meldete bei 90 BPM 9 statt 8.
        const double beatLen = barLen.inSeconds() / 4.0;
        const double to      = from + 2.0 * barLen.inSeconds() - 0.5 * beatLen;

        const auto times = onsetTimes (from, to);

        INFO ("BPM " << bpm);
        REQUIRE (times.size() == 8);            // zwei Takte 4/4

        for (size_t i = 0; i < times.size(); ++i)
        {
            // Jeder Schlag sitzt auf SEINER Taktlinie/Zaehlzeit — nicht
            // irgendwo im Raster verschoben.
            const auto expected = ts.toTime (tracktion::core::BeatPosition::fromBeats (
                                      ts.toBeats (barNine).inBeats() + (double) i)).inSeconds();
            CHECK (times[i] == Catch::Approx (expected).margin (0.003));
        }

        // V2 an dieser Stelle gleich mit: die Eins des Taktes klingt anders als
        // die uebrigen drei Zaehlzeiten.
        const auto downbeat = renderClick (edit, from, from + 0.3);
        const auto beatTwo  = renderClick (edit, times[1], times[1] + 0.3);
        REQUIRE (downbeat.samples.size() == beatTwo.samples.size());
        CHECK (downbeat.samples != beatTwo.samples);
    }
}
