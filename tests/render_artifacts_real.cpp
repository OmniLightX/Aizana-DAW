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
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Diagnose: haengen die Artefakte an der Aenderungsrate", "[.diag][tune2]")
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
    REQUIRE (mono.getMagnitude (0, mono.getNumSamples()) > 0.01f);

    auto dir = outDir ("11_tuning");

    // Klick-Mass ueber die ZWEITE Differenz: ein Knacks ist ein Knick im
    // Signalverlauf, kein blosser Anstieg. Isolierte Ausreisser der zweiten
    // Ableitung treffen genau das (die erste Ableitung war nicht trennscharf,
    // weil steile Nutzsignal-Flanken sie mit ausloesen).
    auto clickScore = [] (const juce::AudioBuffer<float>& b)
    {
        const float* d = b.getReadPointer (0);
        const int n = b.getNumSamples();
        std::vector<double> dd;
        dd.reserve ((size_t) n);
        for (int i = 2; i < n; ++i)
            dd.push_back (std::abs ((double) d[i] - 2.0 * d[i - 1] + d[i - 2]));
        auto sorted = dd;
        std::sort (sorted.begin(), sorted.end());
        const double med = juce::jmax (1.0e-9, sorted[sorted.size() / 2]);
        int spikes = 0;
        for (double v : dd) if (v > med * 60.0) ++spikes;
        return spikes;
    };

    PitchDetector::Settings ps;
    const auto pitch = PitchDetector::detectPitchHz (mono.getReadPointer (0),
                                                     mono.getNumSamples(), sr, ps);
    VocalTuner::Settings vs;
    const auto curve = VocalTuner::buildCorrectionSemitones (pitch, vs, sr / ps.hopSize);

    WARN ("Original: " + std::to_string (clickScore (mono)) + " Knicke");

    // Dieselbe Kurve, aber nur alle N Frames aktualisiert (dazwischen gehalten).
    for (int hold : { 1, 4, 16, 64 })
    {
        juce::Array<float> held;
        for (int i = 0; i < curve.size(); ++i)
            held.add (curve[(i / hold) * hold]);

        juce::AudioBuffer<float> outBuf;
        REQUIRE (VocalTuner::renderCorrected (mono, outBuf, sr, held, ps.hopSize));
        WARN ("Aenderung alle " + std::to_string (hold) + " Frames ("
              + std::to_string ((int) (hold * 1000.0 * ps.hopSize / sr)) + " ms): "
              + std::to_string (clickScore (outBuf)) + " Knicke");
        writeWav (dir.getChildFile ("diag2_hold" + juce::String (hold) + ".wav"), outBuf, sr);
    }

    // Referenz: konstant (klang laut User sauber)
    juce::Array<float> constant;
    for (int i = 0; i < curve.size(); ++i) constant.add (1.0f);
    juce::AudioBuffer<float> constOut;
    REQUIRE (VocalTuner::renderCorrected (mono, constOut, sr, constant, ps.hopSize));
    WARN ("Konstant +1 (Referenz, klingt sauber): "
          + std::to_string (clickScore (constOut)) + " Knicke");
}

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
