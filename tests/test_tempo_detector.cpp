// test_tempo_detector.cpp — Phase 2+: offline BPM estimation

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/TempoDetector.h"

namespace
{
    /** Synthesises a click track: short decaying noise bursts on every beat. */
    juce::AudioBuffer<float> makeClickTrack (double bpm, double seconds, double sampleRate)
    {
        juce::AudioBuffer<float> buf (1, (int) (seconds * sampleRate));
        buf.clear();
        juce::Random rng (42);

        const double beatPeriod = 60.0 / bpm * sampleRate;
        const int clickLen = (int) (0.02 * sampleRate);   // 20 ms burst

        for (double pos = 0.0; pos + clickLen < buf.getNumSamples(); pos += beatPeriod)
        {
            const int start = (int) pos;
            for (int i = 0; i < clickLen; ++i)
            {
                const float env = 1.0f - (float) i / (float) clickLen;
                buf.addSample (0, start + i, (rng.nextFloat() * 2.0f - 1.0f) * env * 0.8f);
            }
        }
        return buf;
    }
}

TEST_CASE ("TempoDetector: detects click-track tempo within 2 BPM", "[tempo]")
{
    const double sr = 44100.0;

    for (double bpm : { 90.0, 120.0, 140.0 })
    {
        auto clicks = makeClickTrack (bpm, 20.0, sr);
        const double detected = TempoDetector::detectBpm (clicks, sr);
        INFO ("expected " << bpm << ", detected " << detected);
        REQUIRE_THAT (detected, Catch::Matchers::WithinAbs (bpm, 2.0));
    }
}

TEST_CASE ("TempoDetector: silence and too-short input return 0", "[tempo]")
{
    const double sr = 44100.0;

    juce::AudioBuffer<float> silence (1, (int) (10.0 * sr));
    silence.clear();
    REQUIRE (TempoDetector::detectBpm (silence, sr) == 0.0);

    juce::AudioBuffer<float> tiny (1, 256);
    tiny.clear();
    REQUIRE (TempoDetector::detectBpm (tiny, sr) == 0.0);

    REQUIRE (TempoDetector::detectBpmInFile (juce::File ("/nonexistent/file.wav")) == 0.0);
}

// ---------------------------------------------------------------------------
// C2 (User-Befund 2026-08-13): Der Beat-Tracker verlor auf einem Kick-Stem den
// Puls und lieferte 81/94/99 BPM — ueberzeugt falsch, ohne jeden Hinweis.
//
// Der Industrieweg ist ablehnen statt raten: Studio One meldet, es koenne "kein
// realistisches Tempo bestimmen, wenn es zu wenig Cadence gibt"; Cubase
// verlangt erkennbare Beats. Aizana misst dafuer, ob unter den gesetzten Beats
// ueberhaupt Anschlaege liegen.
//
// SCHWELLE AUS ECHTEM MATERIAL (Messreihe 2026-08-15, "[confidence]" in
// render_artifacts_real.cpp, Hip-Hop-Session 121 BPM):
//     Perc 0,09 · Clap 0,15 · Gitarre 0,16 · Kick 0,22 · 808 0,43 ·
//     Gesang 0,48 · Akkorde 0,50 || 0,60 || Floete 0,69 · HiHat 0,78 ·
//     DRUM-SUMME 0,79 · VOLLMIX 0,85
// Die realistischen Quellen (Summe, Vollmix) liegen deutlich ueber der
// Schwelle, der gemeldete Fehlerfall deutlich darunter.
// ---------------------------------------------------------------------------
TEST_CASE ("TempoDetector C2: dichtes Material ist verlaesslich, duennes nicht",
           "[tempo][c2][confidence]")
{
    const double sr = 44100.0;

    // Dicht: Anschlag auf JEDEM Beat (120 BPM) — das ist eine Drum-Summe.
    auto dense = makeClickTrack (120.0, 30.0, sr);
    const auto dr = TempoDetector::detectBeatsWithConfidence (dense, sr);
    REQUIRE (dr.beats.size() >= 30);
    INFO ("dicht: Support " << dr.onsetSupport << " Konsistenz " << dr.intervalConsistency);
    CHECK (dr.confidence >= TempoDetector::kMinConfidence);

    // Duenn: nur jeder vierte Beat traegt einen Anschlag (Kick allein, ein
    // Schlag pro Takt). Der DP setzt trotzdem ein Raster — es haengt aber an
    // fast nichts, und genau das muss die Konfidenz zeigen.
    // Duenn: die ersten 8 s tragen jeden Beat (damit die Tempo-Schaetzung auf
    // 120 BPM einrastet), danach nur noch jeden vierten. Genau so sah der
    // gemeldete Fall aus — "wo der Kick aussetzt, verliert der Tracker den
    // Puls": das Raster laeuft mit 0,5 s weiter, findet aber nichts mehr.
    // (Ein von Anfang an duennes Muster reproduziert das NICHT: dann rastet die
    // Schaetzung gleich auf die weiten Abstaende ein und trifft jeden Anschlag.
    // Real gemessen, deshalb steht es hier.)
    juce::AudioBuffer<float> sparse (1, (int) (30.0 * sr));
    sparse.clear();
    juce::Random rng (7);
    const int clickLen = (int) (0.02 * sr);
    for (int beat = 0; beat * 0.5 + 0.02 < 30.0; ++beat)
    {
        const double t = beat * 0.5;
        if (t >= 8.0 && (beat % 4) != 0)
            continue;

        const int start = (int) (t * sr);
        for (int i = 0; i < clickLen; ++i)
        {
            const float env = 1.0f - (float) i / (float) clickLen;
            sparse.addSample (0, start + i, (rng.nextFloat() * 2.0f - 1.0f) * env * 0.8f);
        }
    }

    const auto sr2 = TempoDetector::detectBeatsWithConfidence (sparse, sr);
    INFO ("duenn: " << sr2.beats.size() << " Beats, Support " << sr2.onsetSupport
                    << " Konsistenz " << sr2.intervalConsistency);

    // WICHTIG: es MUSS ein Raster entstanden sein. Sonst wuerde der Test nur
    // "kein Ergebnis" pruefen statt "unzuverlaessiges Ergebnis wird erkannt".
    REQUIRE (sr2.beats.size() >= 20);
    CHECK (sr2.confidence < TempoDetector::kMinConfidence);

    // Und der Abstand ist deutlich, nicht knapp — sonst waere die Schwelle
    // eine Zufallsgrenze.
    CHECK (dr.confidence - sr2.confidence > 0.25);
}

TEST_CASE ("TempoDetector C2: detectBeats bleibt inhaltsgleich zur Konfidenz-Variante",
           "[tempo][c2]")
{
    const double sr = 44100.0;
    auto clicks = makeClickTrack (120.0, 20.0, sr);

    const auto plain = TempoDetector::detectBeats (clicks, sr);
    const auto rich  = TempoDetector::detectBeatsWithConfidence (clicks, sr);

    REQUIRE (plain.size() == rich.beats.size());
    for (int i = 0; i < plain.size(); ++i)
        CHECK (plain[i] == rich.beats[i]);
}
