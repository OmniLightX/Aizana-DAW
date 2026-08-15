// test_tempo_map.cpp — P7a (S1-Parität "Extract to tempo track"):
// Beat-Tracking (Ellis-DP) + Tempo-Map-Anwendung (Grid landet exakt auf den Beats).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/TempoDetector.h"
#include "engine/TempoEngine.h"

#include <cmath>

namespace
{
    constexpr double kSr = 44100.0;

    // Click track: 10 ms 1-kHz-Bursts an den übergebenen Zeiten
    juce::AudioBuffer<float> makeClickBuffer (const juce::Array<double>& clickTimes,
                                              double totalSecs)
    {
        juce::AudioBuffer<float> buf (1, (int) (totalSecs * kSr));
        buf.clear();

        for (double t : clickTimes)
        {
            const int start = (int) (t * kSr);
            const int len   = (int) (0.010 * kSr);
            for (int i = 0; i < len && start + i < buf.getNumSamples(); ++i)
                buf.setSample (0, start + i,
                               0.9f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * 1000.0 * i / kSr));
        }
        return buf;
    }

    juce::Array<double> regularClicks (double startSecs, double dt, int count)
    {
        juce::Array<double> out;
        for (int i = 0; i < count; ++i)
            out.add (startSecs + dt * i);
        return out;
    }

    juce::File writeWav (const juce::File& dir, const juce::String& name,
                         const juce::AudioBuffer<float>& buf)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSr, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }
}

TEST_CASE ("BeatTracking: constant 120 BPM click track is tracked on the beat", "[tempomap][P7a]")
{
    // 40 Klicks alle 0.5 s (120 BPM), 20.5 s Material
    const auto clicks = regularClicks (0.25, 0.5, 40);
    const auto buf    = makeClickBuffer (clicks, 20.5);

    const auto beats = TempoDetector::detectBeats (buf, kSr);
    REQUIRE (beats.size() >= 30);

    // Innere Beats sitzen auf den Klicks (Hop-Auflösung 512/44100 ≈ 11.6 ms)
    int matched = 0;
    for (int i = 2; i < beats.size() - 2; ++i)
    {
        double bestErr = 1.0;
        for (double c : clicks)
            bestErr = juce::jmin (bestErr, std::abs (beats[i] - c));
        if (bestErr < 0.025)
            ++matched;
    }
    CHECK (matched >= (beats.size() - 4) * 8 / 10);   // >= 80 % der inneren Beats

    // Mittleres Intervall entspricht 120 BPM
    double sumDt = 0.0;
    for (int i = 0; i + 1 < beats.size(); ++i)
        sumDt += beats[i + 1] - beats[i];
    const double meanDt = sumDt / (double) (beats.size() - 1);
    CHECK_THAT (60.0 / meanDt, Catch::Matchers::WithinAbs (120.0, 4.0));
}

TEST_CASE ("BeatTracking: tempo change 100 -> 150 BPM is followed", "[tempomap][P7a]")
{
    // 10 s @ 100 BPM (0.6 s) + 10 s @ 150 BPM (0.4 s)
    juce::Array<double> clicks;
    for (int i = 0; i < 16; ++i) clicks.add (0.30 + 0.6 * i);      // bis ~9.3 s
    const double secondStart = 0.30 + 0.6 * 16;                     // 9.9 s
    for (int i = 0; i < 25; ++i) clicks.add (secondStart + 0.4 * i);

    const auto buf   = makeClickBuffer (clicks, 20.5);
    const auto beats = TempoDetector::detectBeats (buf, kSr);
    REQUIRE (beats.size() >= 25);

    // Lokale BPM-Mediane pro Hälfte
    juce::Array<double> first, second;
    for (int i = 0; i + 1 < beats.size(); ++i)
    {
        const double mid = 0.5 * (beats[i] + beats[i + 1]);
        const double bpm = 60.0 / (beats[i + 1] - beats[i]);
        (mid < 9.5 ? first : second).add (bpm);
    }
    REQUIRE (first.size() >= 4);
    REQUIRE (second.size() >= 4);
    first.sort();
    second.sort();

    CHECK_THAT (first[first.size() / 2],   Catch::Matchers::WithinAbs (100.0, 8.0));
    CHECK_THAT (second[second.size() / 2], Catch::Matchers::WithinAbs (150.0, 8.0));
}

TEST_CASE ("TempoMap: grid lands exactly on the supplied beats; undo restores", "[tempomap][P7a]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    TempoEngine engine (em);

    // Beats: 0.5 s Raster ab t=1.0 (120 BPM), dann ab Beat 8 ein 0.4-s-Raster (150 BPM)
    juce::Array<double> beats;
    for (int i = 0; i <= 8; ++i)  beats.add (1.0 + 0.5 * i);        // 1.0 .. 5.0
    for (int i = 1; i <= 8; ++i)  beats.add (5.0 + 0.4 * i);        // 5.4 .. 8.2

    const auto res = engine.extractTempoMap (beats);
    REQUIRE (res.numPoints >= 2);

    auto& seq = edit->tempoSequence;

    // C1: der Vorlauf trägt das ERKANNTE Tempo (120 BPM aus dem 0,5-s-Raster),
    // nicht mehr einen aus der Vorlaufstrecke zurückgerechneten Wert. Der
    // Vorlauf ist ein ganzer Takt (4/4 → 4 Beats), das Raster landet auf den
    // Beats, nachdem das Audio um shiftSeconds gerückt wurde.
    CHECK_THAT (res.leadInBpm, Catch::Matchers::WithinAbs (120.0, 0.5));
    CHECK (res.leadInBeats % 4 == 0);

    for (int k = 0; k < beats.size(); ++k)
    {
        const double t = seq.toTime (tracktion::core::BeatPosition::fromBeats (
                                         (double) (res.leadInBeats + k))).inSeconds();
        CHECK_THAT (t, Catch::Matchers::WithinAbs (beats[k] + res.shiftSeconds, 0.004));
    }

    // Ein Undo stellt die einzelne Grund-Tempo-Map wieder her
    em.getUndoManager().undo();
    CHECK (seq.getNumTempos() == 1);
}

TEST_CASE ("TempoMap: end-to-end from an imported click clip", "[tempomap][P7a]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    TempoEngine engine (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_tempomap_test");
    tempDir.createDirectory();

    // 120-BPM-Klick ab 0.25 s in der DATEI; Clip landet bei 2.0 s im Edit
    const auto clicks = regularClicks (0.25, 0.5, 40);
    const auto wav    = writeWav (tempDir, "clicks.wav", makeClickBuffer (clicks, 20.5));

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (wav, *track, 2.0);
    REQUIRE (clip != nullptr);

    const auto beats = TempoDetector::detectBeatsInFile (wav);
    REQUIRE (beats.size() >= 30);

    const auto res = engine.extractTempoMapFromClip (*clip, beats);
    REQUIRE (res.numPoints >= 1);

    // C1: das Grundtempo ist das erkannte (120 BPM) — vor dem Fix stand hier
    // ein aus der Vorlaufstrecke zurückgerechneter Fantasiewert.
    CHECK_THAT (res.leadInBpm, Catch::Matchers::WithinAbs (120.0, 1.0));

    // Das Audio ist um weniger als einen halben Takt auf die Taktlinie gerückt
    // (Studio Ones „ersten Downbeat auf einen Downbeat ziehen", automatisiert).
    const double barSecs = 4.0 * 60.0 / res.leadInBpm;
    CHECK (std::abs (res.shiftSeconds) <= 0.5 * barSecs + 1.0e-6);

    CHECK_THAT (clip->getPosition().getStart().inSeconds(),
                Catch::Matchers::WithinAbs (2.0 + res.shiftSeconds, 1e-6));

    // Das Grid trifft die Klick-Zeiten im EDIT (Datei-Zeit + Clipstart + Shift):
    // für jeden inneren Klick existiert ein ganzzahliger Beat in seiner Nähe
    auto& seq = edit->tempoSequence;
    int matched = 0;
    for (int k = 4; k < 36; ++k)
    {
        const double clickEdit = clicks[k] + 2.0 + res.shiftSeconds;
        const double beatPos   = seq.toBeats (tracktion::core::TimePosition::fromSeconds (clickEdit)).inBeats();
        if (std::abs (beatPos - std::round (beatPos)) < 0.12)
            ++matched;
    }
    CHECK (matched >= 26);   // >= ~80 % der 32 geprüften Klicks

    tempDir.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Regression 2026-08-10: Tempo-Erkennung auf DICHTEM Material.
//
// Befund aus der Messreihe auf echter Musik (Hip-Hop, 121 BPM): der Detektor
// fand auf KEINER von sieben Quellen das richtige Tempo — und lag dabei nicht
// unsicher daneben, sondern rastete ueberzeugt falsch ein (auf der Gitarre
// sassen 87 % der Beats sauber auf einem 103-BPM-Raster). Ursache war die
// Onset-Erkennung ueber breitbandige Energie plus eine 1/sqrt(lag)-Heuristik,
// die systematisch auf Unterteilungen zog.
//
// Die alten Klicktrack-Tests fingen das nicht, weil ein einzelner Klick pro
// Beat der einfachste denkbare Fall ist. Dieser Test bildet nach, woran der
// Detektor real scheiterte: Schlaege auf JEDEM ACHTEL, aber nur jedes zweite
// ist ein "Beat" (laut), dazwischen leisere HiHat-artige Anschlaege. Wer die
// metrische Ebene verwechselt, landet bei doppeltem Tempo.
// ---------------------------------------------------------------------------
TEST_CASE ("BeatTracking: dichtes Achtel-Material wird nicht doppelt so schnell getrackt",
           "[tempomap][P7a]")
{
    constexpr double bpm     = 121.0;
    constexpr double beatSec = 60.0 / bpm;

    juce::AudioBuffer<float> buf (1, (int) (25.0 * kSr));
    buf.clear();

    // Laute Schlaege auf den Vierteln, leise auf den Achteln dazwischen.
    for (int i = 0; i * (beatSec / 2.0) < 24.0; ++i)
    {
        const double t      = 0.20 + i * (beatSec / 2.0);
        const bool   onBeat = (i % 2) == 0;
        const float  amp    = onBeat ? 0.9f : 0.35f;
        const double freq   = onBeat ? 200.0 : 6000.0;   // Kick-artig vs. HiHat-artig

        const int start = (int) (t * kSr);
        const int len   = (int) ((onBeat ? 0.030 : 0.010) * kSr);
        for (int s = 0; s < len && start + s < buf.getNumSamples(); ++s)
        {
            const double env = std::exp (-6.0 * s / (double) len);
            buf.addSample (0, start + s, amp * (float) (env * std::sin (
                2.0 * juce::MathConstants<double>::pi * freq * s / kSr)));
        }
    }

    // Stufe 1: die globale Schaetzung muss die Viertel treffen, nicht die Achtel.
    const double detected = TempoDetector::detectBpm (buf, kSr);
    INFO ("detectBpm = " << detected);
    CHECK_THAT (detected, Catch::Matchers::WithinAbs (bpm, 4.0));

    // Stufe 2: die Beat-Abstaende muessen Viertel sein. Genau hier lief der
    // Tracker vorher im Achtelraster (157 von 181 Abstaenden waren Achtel).
    const auto beats = TempoDetector::detectBeats (buf, kSr);
    REQUIRE (beats.size() >= 20);

    int quarters = 0, eighths = 0;
    for (int i = 1; i < beats.size(); ++i)
    {
        const double d = beats[i] - beats[i - 1];
        if (std::abs (d - beatSec) < 0.05)             ++quarters;
        else if (std::abs (d - beatSec / 2.0) < 0.04)  ++eighths;
    }
    INFO ("Viertel " << quarters << ", Achtel " << eighths);
    CHECK (quarters > eighths * 4);        // eindeutig auf der Viertel-Ebene
}

// ---------------------------------------------------------------------------
// C1 (User-Befund 2026-08-13): Das Vorlauf-Tempo war ein Fantasiewert.
//
// Reproduktion des gemeldeten Falls: ein 121-BPM-Clip beginnt 0,316 s nach
// Songanfang. In die Vorlaufstrecke passt KEIN ganzer Beat (ein Beat dauert
// 0,496 s), die alte Fassung rundete trotzdem auf einen auf und schrieb
// 60/0,316 = 189,8 BPM als Grundtempo — die Transportanzeige zeigte Unsinn,
// solange der Kopf vor dem Clip stand.
//
// Neu (Weg der Referenz-DAWs): das erkannte Tempo gilt auch im Vorlauf, und
// das Audio rückt auf die nächste Taktlinie. Studio One schreibt genau das als
// Handarbeit vor („drag to line up the first downbeat with a downbeat").
// ---------------------------------------------------------------------------
TEST_CASE ("TempoMap C1: Vorlauf erfindet kein Tempo, Audio rueckt auf die Taktlinie",
           "[tempomap][c1]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    TempoEngine engine (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_tempomap_c1");
    tempDir.createDirectory();

    constexpr double kBpm      = 121.0;
    constexpr double kBeatLen  = 60.0 / kBpm;          // 0.49587 s
    constexpr double kClipStart = 0.316;               // < ein Beat Vorlauf

    // Der Clip enthält das Material; die Beat-Zeiten geben wir exakt vor, damit
    // dieser Test die MAP prüft und nicht den Tracker.
    const auto wav = writeWav (tempDir, "c1.wav",
                               makeClickBuffer (regularClicks (0.0, kBeatLen, 40), 20.5));
    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip  = clipEditor.importAudioFile (wav, *track, kClipStart);
    REQUIRE (clip != nullptr);

    juce::Array<double> beats;
    for (int i = 0; i < 32; ++i)
        beats.add (kClipStart + kBeatLen * i);

    const auto res = engine.extractTempoMap (beats);
    REQUIRE (res.numPoints >= 1);

    // DER EIGENTLICHE BEFUND: kein 189,8-BPM-Vorlauf mehr.
    // Gegenprobe mit der ALTEN Formel (ganze Beat-Anzahl in die Vorlaufstrecke
    // quetschen) — sie ist rein arithmetisch und liefert genau den gemeldeten
    // Wert. Damit ist belegt, dass dieser Test den Fehler wirklich fangen wuerde.
    const double altFormel = 60.0 * (double) juce::jmax (1, (int) std::round (kClipStart / kBeatLen))
                             / kClipStart;
    CHECK_THAT (altFormel, Catch::Matchers::WithinAbs (189.9, 0.5));

    CHECK_THAT (res.leadInBpm, Catch::Matchers::WithinAbs (kBpm, 0.5));
    CHECK (std::abs (res.leadInBpm - altFormel) > 60.0);

    // Der Vorlauf ist ein ganzer Takt (oder null) — der Clip beginnt auf einem
    // Taktanfang, nicht irgendwo zwischen zwei Linien.
    CHECK (res.leadInBeats % 4 == 0);

    // Das Audio ist um weniger als einen halben Takt gerückt und liegt danach
    // exakt auf der Beat-Position, die der Vorlauf ausweist.
    CHECK (std::abs (res.shiftSeconds) <= 0.5 * 4.0 * kBeatLen + 1.0e-6);

    auto& seq = edit->tempoSequence;
    const double gridTime = seq.toTime (tracktion::core::BeatPosition::fromBeats (
                                            (double) res.leadInBeats)).inSeconds();
    CHECK_THAT (clip->getPosition().getStart().inSeconds(),
                Catch::Matchers::WithinAbs (gridTime, 0.002));
    CHECK (clip->getPosition().getStart().inSeconds() >= 0.0);

    // Und das Grundtempo steht auch WIRKLICH in der Sequenz (das ist der Wert,
    // den die Transportanzeige vor dem Clip zeigt).
    REQUIRE (seq.getTempo (0) != nullptr);
    CHECK_THAT (seq.getTempo (0)->getBpm(), Catch::Matchers::WithinAbs (kBpm, 0.5));

    // Ein Undo nimmt Map UND Verschiebung zusammen zurück.
    em.getUndoManager().undo();
    CHECK (seq.getNumTempos() == 1);
    CHECK_THAT (clip->getPosition().getStart().inSeconds(),
                Catch::Matchers::WithinAbs (kClipStart, 1.0e-6));

    tempDir.deleteRecursively();
}

TEST_CASE ("TempoMap C1: Verschiebung drueckt nie Material vor den Songanfang", "[tempomap][c1]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);
    TempoEngine engine (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_tempomap_c1b");
    tempDir.createDirectory();

    constexpr double kBeatLen = 0.5;
    const auto wav = writeWav (tempDir, "c1b.wav",
                               makeClickBuffer (regularClicks (0.0, kBeatLen, 40), 20.5));

    // Clip liegt bei 0.1 s — ein Rückwärts-Ruck auf Beat 0 wäre möglich, ein
    // Ruck über den Nullpunkt hinaus nicht.
    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip  = clipEditor.importAudioFile (wav, *track, 0.1);
    REQUIRE (clip != nullptr);

    juce::Array<double> beats;
    for (int i = 0; i < 32; ++i)
        beats.add (0.1 + kBeatLen * i);

    const auto res = engine.extractTempoMap (beats);
    REQUIRE (res.numPoints >= 1);
    CHECK (clip->getPosition().getStart().inSeconds() >= -1.0e-9);

    tempDir.deleteRecursively();
}
