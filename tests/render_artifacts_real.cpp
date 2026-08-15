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

#include "fx/EQProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include "engine/TempoDetector.h"
#include "engine/PitchDetector.h"
#include "engine/VocalTuner.h"
#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

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


    // Sucht den lautesten zusammenhaengenden Abschnitt. Feste Zeitmarken sind
    // untauglich: in der Rap-Spur war der Ausschnitt ab 0:40 komplett STUMM
    // (RMS 0) — eine Messung darauf sagt nichts, sieht aber nach Ergebnis aus.
    double loudestOffsetSecs (const juce::AudioBuffer<float>& src, double sr, double lengthSecs)
    {
        const int win  = juce::jmax (1, (int) (lengthSecs * sr));
        const int step = juce::jmax (1, (int) (0.5 * sr));
        if (src.getNumSamples() <= win)
            return 0.0;

        double bestRms = -1.0;
        int    bestPos = 0;
        for (int pos = 0; pos + win <= src.getNumSamples(); pos += step)
        {
            double sum = 0.0;
            for (int ch = 0; ch < src.getNumChannels(); ++ch)
            {
                const float* d = src.getReadPointer (ch) + pos;
                for (int i = 0; i < win; i += 16)          // grob genug, viel schneller
                    sum += (double) d[i] * d[i];
            }
            if (sum > bestRms) { bestRms = sum; bestPos = pos; }
        }
        return (double) bestPos / sr;
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

    // Lauteste 15 s statt einer festen Zeitmarke — siehe loudestOffsetSecs().
    const double off = loudestOffsetSecs (full, sr, 15.0);
    auto part = excerpt (full, sr, off, 15.0);
    auto mono = toMono (part);

    auto dir = outDir ("11_tuning");

    // RIEGEL: niemals auf Stille messen. Genau das ist am 2026-08-10 passiert —
    // der Ausschnitt lag auf einer Sprechpause, die Hoerprobe enthielt nur zwei
    // isolierte Impulse in Stille (klingt wie Klicken), und die Cent-Zahlen
    // sahen trotzdem nach einem Ergebnis aus. Ein stiller Ausschnitt ist ein
    // FEHLER im Messaufbau, kein Messergebnis.
    const float partPeak = part.getMagnitude (0, part.getNumSamples());
    INFO ("Ausschnitt ab " << off << " s, Peak " << partPeak);
    REQUIRE (partPeak > 0.01f);

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

    note (dir, "Quelle: " + vox.getFileName() + " (lauteste 15 s, ab "
               + juce::String (off, 1) + " s)");
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

// ─────────────────────────────────────────────────────────────────────────────
// Diagnose P8: der User hoerte im getunten Rendering durchgehend Klicken und
// Verzerrung — die Cent-Messung sah das nicht (naeher am Raster sagt nichts
// ueber Artefakte). Drei Renderings trennen die moeglichen Ursachen:
//   A) Korrekturkurve konstant 0  -> laeuft das Geruest ueberhaupt sauber?
//   B) Korrekturkurve konstant +1 -> stoert ein Pitch-Shift an sich?
//   C) die echte, sich aendernde Kurve -> stoert das staendige Nachfuehren?
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Diagnose: woher kommen die Artefakte im Vocal-Tuning", "[.diag][tune]")
{
    juce::File vox;
    if (auto p = juce::SystemStats::getEnvironmentVariable ("AIZANA_VOCAL_FILE", {}); p.isNotEmpty())
        vox = juce::File (p);
    if (! vox.existsAsFile())
        SKIP ("AIZANA_VOCAL_FILE setzen");

    juce::AudioBuffer<float> full;
    double sr = 0.0;
    REQUIRE (readAudio (vox, full, sr));
    const double off = loudestOffsetSecs (full, sr, 8.0);
    auto mono = toMono (excerpt (full, sr, off, 8.0));
    WARN ("lauteste 8 s ab " + std::to_string (off) + " s");

    auto dir = outDir ("11_tuning");

    // Klick-Mass: Spruenge von Sample zu Sample, die weit ueber dem ueblichen
    // Mass liegen. Ein sauberes Signal hat praktisch keine.
    auto clickScore = [] (const juce::AudioBuffer<float>& b)
    {
        const float* d = b.getReadPointer (0);
        const int n = b.getNumSamples();
        std::vector<double> diffs;
        diffs.reserve ((size_t) n);
        for (int i = 1; i < n; ++i)
            diffs.push_back (std::abs ((double) d[i] - d[i - 1]));
        auto sorted = diffs;
        std::sort (sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        const double thresh = juce::jmax (0.02, med * 40.0);
        int jumps = 0;
        for (double v : diffs) if (v > thresh) ++jumps;
        return std::make_pair (jumps, sorted.back());
    };

    PitchDetector::Settings ps;
    const auto pitch = PitchDetector::detectPitchHz (mono.getReadPointer (0),
                                                     mono.getNumSamples(), sr, ps);
    VocalTuner::Settings vs;
    const auto realCurve = VocalTuner::buildCorrectionSemitones (pitch, vs, sr / ps.hopSize);

    juce::Array<float> zero, constUp;
    for (int i = 0; i < realCurve.size(); ++i) { zero.add (0.0f); constUp.add (1.0f); }

    WARN ("Ausschnitt: " + std::to_string (mono.getNumSamples()) + " Samples, RMS "
          + std::to_string (mono.getRMSLevel (0, 0, mono.getNumSamples()))
          + ", Peak " + std::to_string (mono.getMagnitude (0, mono.getNumSamples())));
    const auto orig = clickScore (mono);
    WARN ("Original                  Spruenge " + std::to_string (orig.first)
          + " | groesster " + std::to_string (orig.second));

    struct Case { const char* name; const juce::Array<float>* curve; const char* file; };
    const Case cases[] = {
        { "A Kurve konstant 0 ", &zero,      "diag_a_kurve_null.wav" },
        { "B Kurve konstant +1", &constUp,   "diag_b_konstant_plus1.wav" },
        { "C echte Kurve      ", &realCurve, "diag_c_echte_kurve.wav" },
    };

    for (const auto& c : cases)
    {
        juce::AudioBuffer<float> outBuf;
        REQUIRE (VocalTuner::renderCorrected (mono, outBuf, sr, *c.curve, ps.hopSize));
        const auto s = clickScore (outBuf);
        WARN (juce::String (c.name).toStdString() + " RMS "
              + std::to_string (outBuf.getRMSLevel (0, 0, outBuf.getNumSamples()))
              + " Peak " + std::to_string (outBuf.getMagnitude (0, outBuf.getNumSamples())));
        WARN (juce::String (c.name).toStdString() + "       Spruenge " + std::to_string (s.first)
              + " | groesster " + std::to_string (s.second));
        writeWav (dir.getChildFile (c.file), outBuf, sr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnose P8 (2): User-Hoerurteil grenzt die Ursache ein — Kurve konstant 0
// und konstant +1 klingen SAUBER, nur die sich aendernde Kurve knackt. Der
// Pitch-Shift an sich ist also in Ordnung; es sind die AENDERUNGEN.
// Hier wird die Aenderungsrate variiert, um den Zusammenhang zu belegen.
TEST_CASE ("Diagnose: was steht ueberhaupt in der Korrekturkurve", "[.diag][curve]")
{
    juce::File vox (juce::SystemStats::getEnvironmentVariable ("AIZANA_VOCAL_FILE", {}));
    if (! vox.existsAsFile()) SKIP ("AIZANA_VOCAL_FILE setzen");

    juce::AudioBuffer<float> full; double sr = 0.0;
    REQUIRE (readAudio (vox, full, sr));
    const double off = loudestOffsetSecs (full, sr, 8.0);
    auto mono = toMono (excerpt (full, sr, off, 8.0));

    PitchDetector::Settings ps;
    const auto pitch = PitchDetector::detectPitchHz (mono.getReadPointer (0),
                                                     mono.getNumSamples(), sr, ps);
    int voiced = 0;
    float pMin = 1.0e9f, pMax = 0.0f;
    for (auto hz : pitch) if (hz > 0.0f) { ++voiced; pMin = std::min (pMin, hz); pMax = std::max (pMax, hz); }
    WARN ("Pitch-Kurve: " + std::to_string (pitch.size()) + " Frames, davon voiced "
          + std::to_string (voiced) + " | Bereich " + std::to_string (pMin) + ".."
          + std::to_string (pMax) + " Hz");

    VocalTuner::Settings vs;
    const auto curve = VocalTuner::buildCorrectionSemitones (pitch, vs, sr / ps.hopSize);

    float cMin = 1.0e9f, cMax = -1.0e9f, maxJump = 0.0f;
    double absSum = 0.0;
    int big = 0;
    for (int i = 0; i < curve.size(); ++i)
    {
        cMin = std::min (cMin, curve[i]);
        cMax = std::max (cMax, curve[i]);
        absSum += std::abs (curve[i]);
        if (std::abs (curve[i]) > 3.0f) ++big;
        if (i > 0) maxJump = std::max (maxJump, std::abs (curve[i] - curve[i - 1]));
    }
    WARN ("Korrekturkurve: Bereich " + std::to_string (cMin) + " .. " + std::to_string (cMax)
          + " Halbtoene | Mittel(abs) " + std::to_string (absSum / curve.size())
          + " | groesster Sprung zwischen zwei Frames " + std::to_string (maxJump)
          + " | Frames mit |Korrektur| > 3 Halbtoene: " + std::to_string (big)
          + " von " + std::to_string (curve.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnose P8 (3): objektives Knacks-Mass.
// Eingang ist ein REINER Sinus. Ein Pitch-Shift daraus ist wieder ein Sinus —
// alles oberhalb ist Artefakt. Klicks sind breitbandig, also messbar als
// Hochfrequenz-Energie. Verglichen werden: konstante Korrektur (laut Hoerurteil
// sauber) gegen eine, die um NULL schwankt (dort kreuzt SoundTouchs Rate die 1.0).

// ─────────────────────────────────────────────────────────────────────────────
// DYN — Dynamik-EQ auf echtem Material (Hoerpaket).
//
// Was der Klang belegen soll: das Band senkt NUR ab, wenn seine Frequenz zu laut
// wird. Deshalb drei Dateien auf demselben Ausschnitt:
//   a_original      — unbehandelt
//   b_statisch      — dasselbe Band als KLASSISCHER Schnitt (immer aktiv)
//   c_dynamisch     — dasselbe Band dynamisch (nur ueber der Schwelle)
// Der Unterschied b→c ist die ganze Behauptung: bei c bleiben die leisen Stellen
// unangetastet. Maschinell gemessen wird genau das — der Pegelunterschied im
// LAUTEN gegen den im LEISEN Abschnitt.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Real: Dynamik-EQ greift nur in den lauten Stellen", "[.realmaterial][dyneq]")
{
    if (! materialDir().isDirectory())
    {
        WARN ("AIZANA_MATERIAL_DIR nicht gesetzt — uebersprungen");
        return;
    }

    auto stem = findStem ("809");                    // Bass-Stem: viel Energie unten
    if (! stem.existsAsFile()) stem = findStem ("Kick");
    REQUIRE (stem.existsAsFile());

    juce::AudioBuffer<float> src;
    double sr = 0.0;
    REQUIRE (readAudio (stem, src, sr));

    auto teil = excerpt (src, sr, loudestOffsetSecs (src, sr, 12.0), 12.0);

    // Die Schwelle MUSS aus dem Material kommen. Erster Anlauf mit geratenen -24 dB:
    // der Detektor hoert das BAND, nicht die Summe, und lag durchgehend darunter —
    // die Dynamik senkte um 0,08 dB ab, also praktisch gar nicht. Also den Pegel im
    // Band messen und die Schwelle 8 dB darunter legen.
    const float bandSchwelleDB = [&]
    {
        auto mono = toMono (teil);
        juce::dsp::IIR::Filter<float> bp;
        bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 90.0, 1.5);
        bp.prepare ({ sr, (juce::uint32) mono.getNumSamples(), 1 });

        float spitze = 0.0f;
        auto* d = mono.getWritePointer (0);
        for (int i = 0; i < mono.getNumSamples(); ++i)
            spitze = juce::jmax (spitze, std::abs (bp.processSample (d[i])));

        return juce::Decibels::gainToDecibels (spitze) - 8.0f;
    }();

    auto machEQ = [&] (bool dynamisch)
    {
        EQProcessor eq;
        for (int i = 0; i < EQProcessor::NumBands; ++i)
        {
            EQBand flat; flat.enabled = false; eq.setBand (i, flat);
        }

        EQBand b;
        b.type           = EQBandType::Peak;
        b.frequencyHz    = 90.0f;      // wo der Bass drueckt
        b.Q              = 1.5f;
        b.enabled        = true;
        b.gainDB         = dynamisch ? 0.0f : -8.0f;   // statisch: fester Schnitt
        b.dynOn          = dynamisch;
        b.dynThresholdDB = bandSchwelleDB;
        b.dynRatio       = 6.0f;
        eq.setBand (2, b);
        eq.setDynTimes (8.0f, 120.0f);

        juce::AudioBuffer<float> out (teil);
        juce::MidiBuffer m;
        eq.prepareToPlay (sr, 512);
        for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        {
            const int len = juce::jmin (512, out.getNumSamples() - pos);
            juce::AudioBuffer<float> blk (out.getArrayOfWritePointers(), out.getNumChannels(), pos, len);
            eq.processBlock (blk, m);
        }
        return out;
    };

    const auto statisch  = machEQ (false);
    const auto dynamisch = machEQ (true);

    auto dir = outDir ("40_dyneq");
    writeWav (dir.getChildFile ("a_original.wav"),  teil,      sr);
    writeWav (dir.getChildFile ("b_statisch.wav"),  statisch,  sr);
    writeWav (dir.getChildFile ("c_dynamisch.wav"), dynamisch, sr);

    // Lautester und leisester Abschnitt (je 1 s) im Original suchen und dort
    // vergleichen. Die Behauptung ist NICHT "leiser", sondern "leiser NUR dort".
    const int fenster = (int) sr;

    // IM BAND messen, nicht breitbandig. Ein Band von Q 1.5 bei 90 Hz aendert an der
    // Gesamt-Energie wenig — der erste Anlauf mass -0,36 dB und sah nach "wirkt kaum"
    // aus, obwohl im Band mehrere dB fehlten. Die Messung muss dieselbe Stelle
    // anschauen, an der das Werkzeug arbeitet.
    auto rms = [sr] (const juce::AudioBuffer<float>& b, int start, int len)
    {
        juce::dsp::IIR::Filter<float> bp;
        bp.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 90.0, 1.5);
        bp.prepare ({ sr, (juce::uint32) len, 1 });

        double s = 0.0;
        for (int i = 0; i < len; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                x += b.getReadPointer (ch)[start + i];
            x /= (float) b.getNumChannels();
            const float y = bp.processSample (x);
            s += (double) y * y;
        }
        return std::sqrt (s / (double) len);
    };

    int lautPos = 0, leisePos = 0;
    double best = -1.0, worst = 1e9;
    for (int pos = 0; pos + fenster <= teil.getNumSamples(); pos += fenster / 2)
    {
        const double r = rms (teil, pos, fenster);
        if (r > best)  { best = r;  lautPos  = pos; }
        if (r < worst) { worst = r; leisePos = pos; }
    }

    const double dynLaut  = 20.0 * std::log10 (rms (dynamisch, lautPos,  fenster) / juce::jmax (1.0e-9, rms (teil, lautPos,  fenster)));
    const double dynLeise = 20.0 * std::log10 (rms (dynamisch, leisePos, fenster) / juce::jmax (1.0e-9, rms (teil, leisePos, fenster)));
    const double statLaut = 20.0 * std::log10 (rms (statisch,  lautPos,  fenster) / juce::jmax (1.0e-9, rms (teil, lautPos,  fenster)));
    const double statLeise= 20.0 * std::log10 (rms (statisch,  leisePos, fenster) / juce::jmax (1.0e-9, rms (teil, leisePos, fenster)));

    note (dir, "Stem: " + stem.getFileName()
               + "   Schwelle aus dem Material: " + juce::String (bandSchwelleDB, 1) + " dB");
    note (dir, juce::String ("gemessen IM BAND (90 Hz, Q 1.5)"));
    note (dir, juce::String ("statisch : laut ") + juce::String (statLaut, 2)
               + " dB / leise " + juce::String (statLeise, 2) + " dB");
    note (dir, juce::String ("dynamisch: laut ") + juce::String (dynLaut, 2)
               + " dB / leise " + juce::String (dynLeise, 2) + " dB");

    // Der statische Schnitt trifft BEIDE Stellen aehnlich stark; der dynamische
    // greift in der lauten deutlich mehr als in der leisen. Genau das ist der
    // Unterschied, den man hoeren soll.
    // Die Behauptung ist nicht "leiser", sondern "leiser NUR dort".
    CHECK (dynLaut < dynLeise);                       // greift in der lauten Stelle staerker
    CHECK (dynLeise > statLeise);                     // laesst die leise Stelle mehr stehen als der feste Schnitt
    CHECK (dynLaut  < -0.5);                          // und tut ueberhaupt etwas Hoerbares
}

// ─────────────────────────────────────────────────────────────────────────────
// P2 — die drei neuen Built-ins auf echtem Material (Hoerpaket).
// Kein Urteil, nur Vorher/Nachher: Delay, Gate und Saturator je einmal trocken
// und einmal bearbeitet, damit der Unterschied direkt vergleichbar ist.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Real: Delay, Gate und Saturator auf echtem Material", "[.realmaterial][p2]")
{
    if (! materialDir().isDirectory())
    {
        WARN ("AIZANA_MATERIAL_DIR nicht gesetzt — uebersprungen");
        return;
    }

    struct Fall { const char* stemFragment; const char* datei; };
    const Fall faelle[] {
        { "Kick",   "gate" },        // Gate an perkussivem Material
        { "chords", "delay" },       // Delay an etwas Getragenem
        { "809",    "saturator" },   // Saturator am Bass
    };

    auto dir = outDir ("41_p2_builtins");

    for (const auto& f : faelle)
    {
        auto stem = findStem (f.stemFragment);
        if (! stem.existsAsFile())
            continue;

        juce::AudioBuffer<float> src;
        double sr = 0.0;
        if (! readAudio (stem, src, sr))
            continue;

        auto teil = excerpt (src, sr, loudestOffsetSecs (src, sr, 10.0), 10.0);
        writeWav (dir.getChildFile (juce::String (f.datei) + "_a_trocken.wav"), teil, sr);

        auto desc = BuiltInPluginFormat::getDescriptionFor (
                        juce::String ("Aizana ") + (juce::String (f.datei) == "gate"      ? "Gate"
                                                  : juce::String (f.datei) == "delay"     ? "Delay"
                                                                                          : "Saturator"));

        juce::String err;
        BuiltInPluginFormat fmt;
        auto inst = fmt.createInstanceFromDescription (desc, sr, 512, err);
        if (inst == nullptr)
        {
            note (dir, juce::String ("KEIN Plugin fuer ") + f.datei + ": " + err);
            continue;
        }

        juce::AudioBuffer<float> out (teil);
        juce::MidiBuffer m;
        inst->prepareToPlay (sr, 512);
        for (int pos = 0; pos < out.getNumSamples(); pos += 512)
        {
            const int len = juce::jmin (512, out.getNumSamples() - pos);
            juce::AudioBuffer<float> blk (out.getArrayOfWritePointers(), out.getNumChannels(), pos, len);
            inst->processBlock (blk, m);
        }

        writeWav (dir.getChildFile (juce::String (f.datei) + "_b_bearbeitet.wav"), out, sr);
        note (dir, juce::String (f.datei) + ": " + stem.getFileName()
                   + "  Spitze trocken " + juce::String (teil.getMagnitude (0, teil.getNumSamples()), 3)
                   + " / bearbeitet "    + juce::String (out.getMagnitude (0, out.getNumSamples()), 3));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C2 — MESSREIHE fuer die Verlaesslichkeits-Schwelle des Beat-Trackers.
//
// Zweck: die Schwelle kMinConfidence aus echtem Material ABLEITEN statt zu
// raten. Gedruckt wird pro Quelle onsetSupport / intervalConsistency /
// confidence. Erwartung: dichte Quellen (Drum-Summe, HiHat, Vollmix) deutlich
// oben, duenne (Kick allein) deutlich unten — dazwischen muss die Schwelle
// mit Abstand zu BEIDEN Seiten liegen.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Messreihe: Beat-Tracker-Konfidenz je Quelle", "[.realmaterial][confidence]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    auto dir = outDir ("50_confidence");

    auto report = [&dir] (const juce::String& label, const TempoDetector::BeatResult& r)
    {
        note (dir, label.paddedRight (' ', 16)
                   + juce::String (r.beats.size()).paddedLeft (' ', 4) + " Beats | Support "
                   + juce::String (r.onsetSupport, 3) + " | Konsistenz "
                   + juce::String (r.intervalConsistency, 3) + " | KONFIDENZ "
                   + juce::String (r.confidence, 3));
    };

    for (const auto* frag : { "Kick", "HH 1", "HH 2", "Clap", "Perc", "809",
                              "Flute", "chords", "Guitar", "Verse 1" })
    {
        auto f = findStem (frag);
        if (! f.existsAsFile()) continue;
        report (frag, TempoDetector::detectBeatsWithConfidenceInFile (f));
    }

    // Drum-Summe und Vollmix = die realistischen Faelle.
    struct Mix { const char* name; std::vector<const char*> parts; };
    for (const auto& mix : { Mix { "DRUM-SUMME", { "Kick", "HH 1", "Clap", "Perc", "809" } },
                             Mix { "VOLLMIX",    { "Kick", "HH 1", "Clap", "Perc", "809",
                                                   "chords", "Verse 1" } } })
    {
        juce::AudioBuffer<float> sum; double sr = 0.0; int added = 0;
        for (const auto* frag : mix.parts)
        {
            auto f = findStem (frag);
            if (! f.existsAsFile()) continue;
            juce::AudioBuffer<float> b; double thisSr = 0.0;
            if (! readAudio (f, b, thisSr)) continue;
            if (added == 0) { sr = thisSr; sum.setSize (1, b.getNumSamples()); sum.clear(); }
            const int n = juce::jmin (sum.getNumSamples(), b.getNumSamples());
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                sum.addFrom (0, 0, b, ch, 0, n, 0.4f / (float) b.getNumChannels());
            ++added;
        }
        if (added >= 3)
            report (mix.name, TempoDetector::detectBeatsWithConfidence (sum, sr));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C3 — MESSREIHE fuer den Tonumfang des Pitch-Detektors.
//
// Befund: Vocal-Tuning lehnte einen Floeten-Stem als "kein tonales Material"
// ab. Verdacht: der Standard-Suchbereich endet bei 1000 Hz. Gemessen wird der
// Anteil stimmhafter Frames und der Median je Bereich — daraus faellt die
// Entscheidung, ob ein zweiter Durchgang mit weiterem Bereich das Problem
// loest (Melodyne macht genau das ueber die Variante "Tonal (hoch)").
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Messreihe: Pitch-Detektor je Tonumfang", "[.realmaterial][pitchrange]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    auto dir = outDir ("51_pitchrange");

    struct RangeDef { const char* name; float minHz; float maxHz; };
    const RangeDef ranges[] = { { "Standard 60-1000",  60.0f, 1000.0f },
                                { "Hoch     150-2500", 150.0f, 2500.0f },
                                { "Weit      60-2500",  60.0f, 2500.0f } };

    for (const auto* frag : { "Flute", "Kalimba Hook", "Verse 1", "Chorus Main 1", "809", "chords" })
    {
        auto f = findStem (frag);
        if (! f.existsAsFile()) continue;

        juce::AudioBuffer<float> b; double sr = 0.0;
        if (! readAudio (f, b, sr)) continue;
        auto mono = toMono (b);

        // Auf den lautesten 10 s messen — Stille am Anfang verzerrt den Anteil.
        const double off = loudestOffsetSecs (mono, sr, 10.0);
        auto part = excerpt (mono, sr, off, 10.0);

        for (const auto& r : ranges)
        {
            PitchDetector::Settings s;
            s.minHz = r.minHz;
            s.maxHz = r.maxHz;
            const auto curve = PitchDetector::detectPitchHz (part.getReadPointer (0),
                                                             part.getNumSamples(), sr, s);
            int voiced = 0;
            for (float v : curve) if (v > 0.0f) ++voiced;
            const double ratio = curve.isEmpty() ? 0.0
                                                 : (double) voiced / (double) curve.size();

            note (dir, juce::String (frag).paddedRight (' ', 16)
                       + juce::String (r.name).paddedRight (' ', 20)
                       + "stimmhaft " + juce::String (100.0 * ratio, 1).paddedLeft (' ', 5)
                       + " % | Median " + juce::String (PitchDetector::medianPitchHz (curve), 1)
                       + " Hz");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C3 — DIAGNOSE: warum lehnt tuneClipInPlace den Floeten-Stem ab?
// Die Sammelmeldung im Dialog nennt vier moegliche Ursachen auf einmal. Hier
// wird jede EINZELN geprueft, damit die Behauptung belegt ist statt vermutet.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Diagnose: Vocal-Tuning auf echtem Fremdmaterial", "[.realmaterial][tunediag]")
{
    if (! materialDir().isDirectory())
        SKIP ("Kein Material — AIZANA_MATERIAL_DIR setzen");

    auto dir = outDir ("52_tunediag");

    for (const auto* frag : { "Flute", "Kalimba Hook", "Verse 1", "chords" })
    {
        auto f = findStem (frag);
        if (! f.existsAsFile()) continue;

        EditManager em;
        em.createNewEdit();
        ClipEditor clipEditor (em);

        auto* track = te::getAudioTracks (*em.getEdit())[0];
        auto* clip  = clipEditor.importAudioFile (f, *track, 1.0);
        if (clip == nullptr) { note (dir, juce::String (frag) + ": Import fehlgeschlagen"); continue; }

        const auto pos = clip->getPosition();
        juce::String diag = juce::String (frag).paddedRight (' ', 16)
                          + "speedRatio " + juce::String (clip->getSpeedRatio(), 4)
                          + " | autoTempo " + (clip->getAutoTempo() ? "AN" : "aus")
                          + " | Laenge " + juce::String (pos.getLength().inSeconds(), 2) + " s"
                          + " | Offset " + juce::String (pos.getOffset().inSeconds(), 2) + " s";

        VocalTuner tuner (em);
        auto* tuned = tuner.tuneClipInPlace (*clip, {});
        diag += juce::String (" | tuneClipInPlace ") + (tuned != nullptr ? "OK" : "ABGELEHNT");
        note (dir, diag);
    }
}
