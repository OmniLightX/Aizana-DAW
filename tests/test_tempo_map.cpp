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

    const int numPoints = engine.extractTempoMap (beats);
    REQUIRE (numPoints >= 2);

    auto& seq = edit->tempoSequence;

    // Vorlauf: t0=1.0, Median-dt≈0.5 → 2 Beats Vorlauf; Beat 2 == 1.0 s,
    // danach landet Beat (2+k) exakt auf beats[k]
    for (int k = 0; k < beats.size(); ++k)
    {
        const double t = seq.toTime (tracktion::core::BeatPosition::fromBeats ((double) (2 + k))).inSeconds();
        CHECK_THAT (t, Catch::Matchers::WithinAbs (beats[k], 0.004));
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

    const int numPoints = engine.extractTempoMapFromClip (*clip, beats);
    REQUIRE (numPoints >= 1);

    // Das Grid trifft die Klick-Zeiten im EDIT (Datei-Zeit + 2.0 s):
    // für jeden inneren Klick existiert ein ganzzahliger Beat in seiner Nähe
    auto& seq = edit->tempoSequence;
    int matched = 0;
    for (int k = 4; k < 36; ++k)
    {
        const double clickEdit = clicks[k] + 2.0;
        const double beatPos   = seq.toBeats (tracktion::core::TimePosition::fromSeconds (clickEdit)).inBeats();
        if (std::abs (beatPos - std::round (beatPos)) < 0.12)
            ++matched;
    }
    CHECK (matched >= 26);   // >= ~80 % der 32 geprüften Klicks

    // Audio blieb stehen: Clip-Position unverändert
    CHECK_THAT (clip->getPosition().getStart().inSeconds(),
                Catch::Matchers::WithinAbs (2.0, 1e-9));

    tempDir.deleteRecursively();
}
