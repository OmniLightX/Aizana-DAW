// render_artifacts_real.cpp — Hoer-Pakete auf ECHTEM Material.
//
// Gegenstueck zu render_artifacts.cpp (dort: synthetische Instrumente). Hier
// laufen die Features, die ohne echte Musik nichts aussagen, gegen die Stems
// des Users. Jeder Fall misst, was maschinell entscheidbar ist, und legt
// zusaetzlich Vorher/Nachher-Dateien zum Gegenhoeren ab.
//
// Alle Cases tragen das versteckte Tag "[.realmaterial]" → sie laufen NICHT im
// normalen Suite-Lauf. Aufruf:
//
//     AIZANA_MATERIAL_DIR="/pfad/zu/den/stems" \
//     AIZANA_ARTIFACT_DIR="/pfad/zur/ausgabe" \
//       engine_tests "[realmaterial]"
//
// Ohne AIZANA_MATERIAL_DIR ueberspringen sich die Faelle selbst (SKIP), damit
// die Datei auf jedem Rechner uebersetzt und harmlos bleibt.
#include <catch2/catch_test_macros.hpp>

#include "engine/TempoDetector.h"
#include "engine/PitchDetector.h"
#include "engine/VocalTuner.h"

#include <cmath>
#include <vector>

namespace
{
    juce::File materialDir()
    {
        auto p = juce::SystemStats::getEnvironmentVariable ("AIZANA_MATERIAL_DIR", {});
        return p.isNotEmpty() ? juce::File (p) : juce::File();
    }

    juce::File outDir (const juce::String& sub)
    {
        auto base = juce::SystemStats::getEnvironmentVariable ("AIZANA_ARTIFACT_DIR", {});
        auto d = base.isNotEmpty()
                   ? juce::File (base)
                   : juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                         .getChildFile ("Aizana Testlaeufe").getChildFile ("real");
        d = d.getChildFile (sub);
        d.createDirectory();
        return d;
    }

    // Findet einen Stem ueber ein Namensfragment (die Dateinamen des Users
    // enthalten Tempo-Praefixe und Zaehler-Suffixe, deshalb kein exakter Name).
    juce::File findStem (const juce::String& fragment)
    {
        auto dir = materialDir();
        if (! dir.isDirectory())
            return {};

        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
            if (f.getFileNameWithoutExtension().containsIgnoreCase (fragment))
                return f;
        return {};
    }

    bool readAudio (const juce::File& file, juce::AudioBuffer<float>& dest, double& sampleRate)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;

        sampleRate = reader->sampleRate;
        dest.setSize ((int) juce::jmin ((juce::uint32) 2, reader->numChannels),
                      (int) reader->lengthInSamples);
        reader->read (&dest, 0, dest.getNumSamples(), 0, true, dest.getNumChannels() > 1);
        return true;
    }

    // Schneidet einen Ausschnitt heraus (Hoerproben kurz halten).
    juce::AudioBuffer<float> excerpt (const juce::AudioBuffer<float>& src, double sr,
                                      double startSecs, double lengthSecs)
    {
        const int start = juce::jlimit (0, src.getNumSamples() - 1, (int) (startSecs * sr));
        const int len   = juce::jmin ((int) (lengthSecs * sr), src.getNumSamples() - start);
        juce::AudioBuffer<float> out (src.getNumChannels(), len);
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            out.copyFrom (ch, 0, src, ch, start, len);
        return out;
    }

    void writeWav (const juce::File& out, const juce::AudioBuffer<float>& buf, double sr)
    {
        out.deleteFile();
        juce::WavAudioFormat fmt;
        if (auto os = out.createOutputStream())
            if (std::unique_ptr<juce::AudioFormatWriter> w (
                    fmt.createWriterFor (os.release(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
                w != nullptr)
                w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    void note (const juce::File& dir, const juce::String& line)
    {
        dir.getChildFile ("befund.txt").appendText (line + "\n");
        WARN (line.toStdString());
    }

    juce::AudioBuffer<float> toMono (const juce::AudioBuffer<float>& src)
    {
        juce::AudioBuffer<float> mono (1, src.getNumSamples());
        mono.clear();
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            mono.addFrom (0, 0, src, ch, 0, src.getNumSamples(), 1.0f / (float) src.getNumChannels());
        return mono;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// P7a — Tempo-Erkennung auf echtem Schlagzeug.
// Das Material ist auf 121 BPM produziert: der Detektor MUSS darauf landen.
// Das ist der schaerfste Test des Beat-Trackers, den es ohne Handarbeit gibt —
// die Wahrheit ist bekannt, also ist "ungefaehr richtig" nachweisbar falsch.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Real: Tempo-Erkennung findet die 121 BPM des Materials", "[.realmaterial][tempo]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    auto dir = outDir ("10_tempo");

    // Ueber MEHRERE Quellen messen: ein Beat-Tracker haengt stark von der
    // Onset-Dichte ab. Eine duenn besetzte Kick-Spur ist der schwerste Fall,
    // eine HiHat- oder Percussion-Spur der leichteste. Nur so laesst sich
    // "Detektor kaputt" von "Quelle zu duenn" unterscheiden.
    const char* candidates[] = { "Kick", "HH 1", "HH 2", "Clap", "Perc", "Guitar", "chords" };

    int hits = 0, tried = 0;
    for (const auto* frag : candidates)
    {
        auto f = findStem (frag);
        if (! f.existsAsFile())
            continue;

        // Stufe 1 (globale Tempo-Schaetzung) getrennt von Stufe 2 (Beat-
        // Platzierung) messen — sonst verdeckt ein Fehler den anderen.
        {
            juce::AudioBuffer<float> b; double thisSr = 0.0;
            if (readAudio (f, b, thisSr))
            {
                auto m = toMono (b);
                note (dir, juce::String (frag).paddedRight (' ', 10)
                           + "Stufe 1 detectBpm = "
                           + juce::String (TempoDetector::detectBpm (m, thisSr), 2));
            }
        }

        const auto beats = TempoDetector::detectBeatsInFile (f);
        if (beats.size() < 8)
        {
            note (dir, juce::String (frag).paddedRight (' ', 10) + " zu wenige Beats erkannt ("
                       + juce::String (beats.size()) + ")");
            continue;
        }

        std::vector<double> gaps;
        for (int i = 1; i < beats.size(); ++i)
            gaps.push_back (beats[i] - beats[i - 1]);
        auto sorted = gaps;
        std::sort (sorted.begin(), sorted.end());
        const double medianGap = sorted[sorted.size() / 2];
        const double meanGap   = (beats[beats.size() - 1] - beats[0]) / (double) gaps.size();

        // Die eigentliche Frage ist NICHT "wie gross ist der mittlere Abstand",
        // sondern "sitzt ein 121-BPM-Raster auf den erkannten Beats". Bei
        // ungleichmaessigen Abstaenden (Pausen, halbe Takte) ist der Median
        // irrefuehrend — das Raster-Mass ist es nicht.
        auto gridFit = [&beats] (double bpm)
        {
            const double period = 60.0 / bpm;
            int best = 0;
            for (int p = 0; p < 40; ++p)                   // Phase in 1/40-Schritten
            {
                const double phase = period * p / 40.0;
                int inGrid = 0;
                for (auto t : beats)
                {
                    const double rel = std::fmod (t - phase + period * 100.0, period);
                    const double dist = std::min (rel, period - rel);
                    if (dist < 0.030) ++inGrid;            // 30 ms Toleranz
                }
                best = std::max (best, inGrid);
            }
            return 100.0 * best / (double) beats.size();
        };

        const double fit121 = gridFit (121.0);
        const double fitOwn = gridFit (60.0 / medianGap);

        ++tried;
        const bool ok = fit121 > 80.0;                     // Raster sitzt auf den Beats
        if (ok) ++hits;

        note (dir, juce::String (frag).paddedRight (' ', 10)
                   + juce::String (beats.size()).paddedLeft (' ', 4) + " Beats | Median "
                   + juce::String (60.0 / medianGap, 1).paddedLeft (' ', 6) + " BPM | Mittel "
                   + juce::String (60.0 / meanGap, 1).paddedLeft (' ', 6) + " BPM | auf 121er-Raster: "
                   + juce::String (fit121, 0).paddedLeft (' ', 3) + " % | auf eigenem Raster: "
                   + juce::String (fitOwn, 0).paddedLeft (' ', 3) + " %   "
                   + (ok ? "OK" : "ABWEICHUNG"));
    }

    // Realistischster Fall: eine DRUM-SUMME (so sieht ein importierter Loop oder
    // eine Schlagzeugspur aus), nicht eine isolierte Einzelspur.
    {
        juce::AudioBuffer<float> sum;
        double sr = 0.0;
        int added = 0;
        for (const auto* frag : { "Kick", "HH 1", "Clap", "Perc", "809" })
        {
            auto f = findStem (frag);
            if (! f.existsAsFile()) continue;

            juce::AudioBuffer<float> b;
            double thisSr = 0.0;
            if (! readAudio (f, b, thisSr)) continue;

            if (added == 0) { sr = thisSr; sum.setSize (1, b.getNumSamples()); sum.clear(); }
            const int n = juce::jmin (sum.getNumSamples(), b.getNumSamples());
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                sum.addFrom (0, 0, b, ch, 0, n, 0.4f / (float) b.getNumChannels());
            ++added;
        }

        if (added >= 3)
        {
            auto mixFile = dir.getChildFile ("drum_summe.wav");
            writeWav (mixFile, sum, sr);
            const auto beats = TempoDetector::detectBeatsInFile (mixFile);

            note (dir, "Drum-Summe Stufe 1 detectBpm = "
                       + juce::String (TempoDetector::detectBpm (sum, sr), 2));

            if (beats.size() > 8)
            {
                std::vector<double> g;
                for (int i = 1; i < beats.size(); ++i) g.push_back (beats[i] - beats[i - 1]);
                auto sorted2 = g; std::sort (sorted2.begin(), sorted2.end());

                // Wie oft trifft der Abstand das SOLL-Intervall (0.4959 s)?
                int onBeat = 0, halfBeat = 0, other = 0;
                for (double d : g)
                {
                    if (std::abs (d - 0.4959) < 0.04)       ++onBeat;
                    else if (std::abs (d - 0.2479) < 0.03)  ++halfBeat;
                    else                                     ++other;
                }
                note (dir, "Drum-Summe: " + juce::String (beats.size()) + " Beats | Median "
                           + juce::String (60.0 / sorted2[sorted2.size() / 2], 1) + " BPM | Abstaende: "
                           + juce::String (onBeat) + "x Viertel, " + juce::String (halfBeat)
                           + "x Achtel, " + juce::String (other) + "x anderes");
            }
        }
    }

    REQUIRE (tried > 0);
    note (dir, "Ergebnis: " + juce::String (hits) + " von " + juce::String (tried)
               + " Quellen ergaben 121 BPM (+-3).");

    // Anspruch: auf MINDESTENS der Haelfte der Quellen muss das Tempo stimmen.
    // Faellt der Detektor auf allen durch, ist er kaputt; faellt er nur auf der
    // duennen Kick durch, ist das eine dokumentierte Grenze, kein Fehler.
    CHECK (hits * 2 >= tried);
}

// ─────────────────────────────────────────────────────────────────────────────
// P8 — Vocal-Tuning auf einer echten Gesangsspur.
// Synthetisch (Sinus) war das schon bewiesen; echte Stimme ist der Fall, in dem
// YIN scheitern kann (Vibrato, Konsonanten, Atmer, Raum).
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Real: Vocal-Tuning zieht eine echte Gesangsspur auf die Skala", "[.realmaterial][tune]")
{
    // Bevorzugt eine ROHE, ungetunte Aufnahme (AIZANA_VOCAL_FILE). Fertig
    // produzierte Stems sind meist schon getunt — daran laesst sich nicht
    // messen, ob das Tuning etwas taugt, weil es nichts zu korrigieren gibt.
    juce::File vox;
    if (auto p = juce::SystemStats::getEnvironmentVariable ("AIZANA_VOCAL_FILE", {}); p.isNotEmpty())
        vox = juce::File (p);
    if (! vox.existsAsFile()) vox = findStem ("Chorus Main 1");
    if (! vox.existsAsFile()) vox = findStem ("Verse 1");
    if (! vox.existsAsFile())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR oder AIZANA_VOCAL_FILE setzen");

    juce::AudioBuffer<float> full;
    double sr = 0.0;
    REQUIRE (readAudio (vox, full, sr));

    // 15 s aus der Mitte: dort singt jemand, am Anfang ist oft Stille.
    auto part = excerpt (full, sr, 40.0, 15.0);
    auto mono = toMono (part);

    auto dir = outDir ("11_tuning");
    writeWav (dir.getChildFile ("a_original.wav"), part, sr);

    PitchDetector::Settings ps;                 // hopSize steckt in den Settings
    const int hop = ps.hopSize;
    const auto pitchBefore = PitchDetector::detectPitchHz (mono.getReadPointer (0),
                                                           mono.getNumSamples(), sr, ps);
    const float medBefore = PitchDetector::medianPitchHz (pitchBefore);

    VocalTuner::Settings s;
    const double frameRate = sr / (double) hop;
    const auto correction = VocalTuner::buildCorrectionSemitones (pitchBefore, s, frameRate);

    juce::AudioBuffer<float> tuned;
    REQUIRE (VocalTuner::renderCorrected (mono, tuned, sr, correction, hop));
    writeWav (dir.getChildFile ("b_getunt.wav"), tuned, sr);

    const auto pitchAfter = PitchDetector::detectPitchHz (tuned.getReadPointer (0),
                                                          tuned.getNumSamples(), sr, ps);

    // Kernaussage: nach dem Tunen liegen die stimmhaften Frames NAEHER an einem
    // Halbton-Raster als vorher. Das ist die messbare Definition von "sitzt".
    auto centsOffGrid = [] (const juce::Array<float>& curve)
    {
        double sum = 0.0; int n = 0;
        for (auto hz : curve)
        {
            if (hz <= 0.0f) continue;                      // unvoiced
            const double midi = VocalTuner::hzToMidi (hz);
            sum += std::abs (midi - std::round (midi)) * 100.0;
            ++n;
        }
        return n > 0 ? sum / n : -1.0;
    };

    const double offBefore = centsOffGrid (pitchBefore);
    const double offAfter  = centsOffGrid (pitchAfter);

    note (dir, "Quelle: " + vox.getFileName() + " (15 s ab 0:40)");
    note (dir, "Median-Tonhoehe vorher " + juce::String (medBefore, 1) + " Hz");
    note (dir, "Mittlere Abweichung vom Halbton-Raster: vorher "
               + juce::String (offBefore, 1) + " Cent, nachher "
               + juce::String (offAfter, 1) + " Cent");

    CHECK (offBefore > 0.0);
    CHECK (offAfter  < offBefore);      // naeher am Raster = das Tuning wirkt

    // Diagnose: wie weit KANN der Tuner ueberhaupt ans Raster heran? Trennt
    // "Glaettung bremst" (smoothingMs) von "Pitch-Shift ungenau" (Rest bleibt
    // auch ohne Glaettung stehen).
    for (float sm : { 35.0f, 10.0f, 2.0f })
    {
        VocalTuner::Settings v;
        v.smoothingMs = sm;
        const auto corr = VocalTuner::buildCorrectionSemitones (pitchBefore, v, frameRate);
        juce::AudioBuffer<float> out;
        if (! VocalTuner::renderCorrected (mono, out, sr, corr, hop))
            continue;
        const auto pc = PitchDetector::detectPitchHz (out.getReadPointer (0),
                                                      out.getNumSamples(), sr, ps);
        note (dir, "  Glaettung " + juce::String (sm, 0).paddedLeft (' ', 3) + " ms -> "
                   + juce::String (centsOffGrid (pc), 1) + " Cent Restabweichung");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnose des Tempo-Detektors: erst synthetische Klicktracks mit BEKANNTEM
// Tempo (dort muss er perfekt sein), dann echtes Material. Trennt "Onset-Kurve
// kaputt" von "Tempo-Schaetzung kaputt".
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Diagnose: Tempo-Schaetzung auf Klicktracks", "[.diag][tempo]")
{
    constexpr double sr = 44100.0;

    auto clickTrack = [&] (double bpm, double secs)
    {
        juce::AudioBuffer<float> buf (1, (int) (secs * sr));
        buf.clear();
        const double dt = 60.0 / bpm;
        for (double t = 0.0; t < secs; t += dt)
        {
            const int start = (int) (t * sr);
            const int len   = (int) (0.010 * sr);
            for (int i = 0; i < len && start + i < buf.getNumSamples(); ++i)
                buf.setSample (0, start + i, 0.9f * (float) std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
        }
        return buf;
    };

    // Tempowechsel-Signal wie im Golden-Test: 100 BPM, dann 150 BPM.
    {
        juce::AudioBuffer<float> buf (1, (int) (20.5 * sr));
        buf.clear();
        std::vector<double> clicks;
        for (int i = 0; i < 16; ++i) clicks.push_back (0.30 + 0.6 * i);
        const double secondStart = 0.30 + 0.6 * 16;
        for (int i = 0; i < 25; ++i) clicks.push_back (secondStart + 0.4 * i);
        for (double t : clicks)
        {
            const int start = (int) (t * sr);
            const int len   = (int) (0.010 * sr);
            for (int i = 0; i < len && start + i < buf.getNumSamples(); ++i)
                buf.setSample (0, start + i, 0.9f * (float) std::sin (
                    2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
        }
        WARN ("Wechsel 100->150: globales detectBpm = "
              + std::to_string (TempoDetector::detectBpm (buf, sr)));
        const auto bts = TempoDetector::detectBeats (buf, sr);
        juce::String line;
        for (int i = 0; i < juce::jmin (24, bts.size()); ++i)
            line += juce::String (bts[i], 3) + " ";
        WARN ("  erkannte Beats: " + line.toStdString());
        juce::String iv;
        for (int i = 1; i < juce::jmin (20, bts.size()); ++i)
            iv += juce::String (bts[i] - bts[i-1], 3) + " ";
        WARN ("  Intervalle:     " + iv.toStdString());
    }

    for (double bpm : { 90.0, 100.0, 120.0, 140.0, 121.0 })
    {
        auto buf = clickTrack (bpm, 20.0);
        const double got   = TempoDetector::detectBpm (buf, sr);
        const auto   beats = TempoDetector::detectBeats (buf, sr);
        WARN ("Klick " + std::to_string ((int) bpm) + " BPM -> detectBpm "
              + std::to_string (got) + " | Beats " + std::to_string (beats.size()));
    }
}
