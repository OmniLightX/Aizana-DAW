// test_vocal_tuner.cpp — P8 Vocal-Tuning (offline, Bounce-Weg)
//
// Kernaussagen:
//   - PitchDetector (YIN) erkennt f0 eines Sinus präzise; Stille = unvoiced
//   - snapMidiToScale: chromatisch rundet, Dur/Moll snappt auf Skalentöne
//   - buildCorrectionSemitones: 445 Hz → ≈ −0.196 st Richtung A4 (440)
//   - RENDER-BEWEIS: verstimmter Sinus-Clip → tuneClipInPlace → neue Datei
//     misst ≈ 440 Hz; EIN Undo stellt den Original-Clip wieder her

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/PitchDetector.h"
#include "engine/VocalTuner.h"

#include <cmath>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 44100.0;

    juce::AudioBuffer<float> makeSine (double freqHz, double seconds, float amp = 0.5f)
    {
        juce::AudioBuffer<float> buf (1, (int) (seconds * kSampleRate));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freqHz * i / kSampleRate));
        return buf;
    }

    juce::File writeWav (const juce::AudioBuffer<float>& buf, const juce::File& dir,
                         const juce::String& name)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    float detectFileMedianHz (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, false);

        const auto curve = PitchDetector::detectPitchHz (buf.getReadPointer (0),
                                                         buf.getNumSamples(), reader->sampleRate);
        return PitchDetector::medianPitchHz (curve);
    }
}

TEST_CASE ("PitchDetector: YIN finds sine f0, silence is unvoiced", "[tune][p8]")
{
    const auto sine = makeSine (440.0, 1.0);
    const auto curve = PitchDetector::detectPitchHz (sine.getReadPointer (0),
                                                     sine.getNumSamples(), kSampleRate);
    REQUIRE (! curve.isEmpty());
    CHECK (PitchDetector::medianPitchHz (curve) == Approx (440.0).margin (1.0));

    // Verstimmt: 445 Hz wird als 445 erkannt (nicht auf 440 gezogen)
    const auto off = makeSine (445.0, 1.0);
    const auto curve2 = PitchDetector::detectPitchHz (off.getReadPointer (0),
                                                      off.getNumSamples(), kSampleRate);
    CHECK (PitchDetector::medianPitchHz (curve2) == Approx (445.0).margin (1.0));

    // Stille = durchgehend unvoiced
    juce::AudioBuffer<float> silence (1, 44100);
    silence.clear();
    const auto curve3 = PitchDetector::detectPitchHz (silence.getReadPointer (0),
                                                      silence.getNumSamples(), kSampleRate);
    CHECK (PitchDetector::medianPitchHz (curve3) == 0.0f);
}

TEST_CASE ("VocalTuner: scale snapping (chromatic / major / minor)", "[tune][p8]")
{
    using S = VocalTuner::Scale;

    // Chromatisch: einfach runden
    CHECK (VocalTuner::snapMidiToScale (69.4, 0, S::chromatic) == 69);
    CHECK (VocalTuner::snapMidiToScale (69.6, 0, S::chromatic) == 70);

    // C-Dur: F#4 (66) liegt zwischen F (65) und G (67) — Gleichstand → abwärts
    CHECK (VocalTuner::snapMidiToScale (66.0, 0, S::major) == 65);
    // 66.2 ist näher an G (67)? Abstand 0.8 zu 67, 1.2 zu 65 → 67
    CHECK (VocalTuner::snapMidiToScale (66.3, 0, S::major) == 67);
    // Skalenton bleibt liegen
    CHECK (VocalTuner::snapMidiToScale (64.0, 0, S::major) == 64);   // E4

    // A-Moll (root 9): H (71) ist Skalenton, B♭(70) nicht → 70 snappt (Gleichstand abwärts auf A=69)
    CHECK (VocalTuner::snapMidiToScale (71.0, 9, S::minor) == 71);
    CHECK (VocalTuner::snapMidiToScale (70.0, 9, S::minor) == 69);

    // hz↔midi Roundtrip
    CHECK (VocalTuner::hzToMidi (440.0) == Approx (69.0).margin (1e-9));
    CHECK (VocalTuner::midiToHz (69.0)  == Approx (440.0).margin (1e-6));
}

TEST_CASE ("VocalTuner: correction curve pulls 445 Hz toward A4", "[tune][p8]")
{
    juce::Array<float> pitch;
    for (int i = 0; i < 100; ++i)
        pitch.add (445.0f);

    VocalTuner::Settings st;               // chromatisch, volle Stärke
    st.smoothingMs = 0.0f;                 // Roh-Korrektur prüfen
    const auto corr = VocalTuner::buildCorrectionSemitones (pitch, st, 86.0);

    REQUIRE (corr.size() == 100);
    // 445 Hz = MIDI 69.1956… → Ziel 69 → Korrektur ≈ −0.1956 Halbtöne
    CHECK (corr[50] == Approx (-0.1956).margin (0.005));

    // Halbe Stärke halbiert die Korrektur
    st.strength = 0.5f;
    const auto half = VocalTuner::buildCorrectionSemitones (pitch, st, 86.0);
    CHECK (half[50] == Approx (-0.0978).margin (0.005));

    // Unvoiced hält die letzte Korrektur (kein Zip)
    pitch.set (60, 0.0f);
    st.strength = 1.0f;
    const auto held = VocalTuner::buildCorrectionSemitones (pitch, st, 86.0);
    CHECK (held[60] == Approx (held[59]).margin (1e-6));
}

TEST_CASE ("VocalTuner: RENDER PROOF — detuned clip is pulled onto pitch, one undo restores",
           "[tune][p8]")
{
    EditManager em;
    em.createNewEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_tune_test");
    tempDir.createDirectory();
    const auto srcWav = writeWav (makeSine (445.0, 1.5), tempDir, "detuned.wav");

    auto* track = te::getAudioTracks (*em.getEdit())[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 1.0);
    REQUIRE (clip != nullptr);
    const auto rangeBefore = clip->getPosition().time;

    VocalTuner tuner (em);
    VocalTuner::Settings st;   // chromatisch, 100 %
    auto* tuned = tuner.tuneClipInPlace (*clip, st);
    REQUIRE (tuned != nullptr);

    // Position identisch, neue Quelle, Name markiert
    CHECK (tuned->getPosition().time.getStart() == rangeBefore.getStart());
    CHECK (tuned->getPosition().time.getEnd()   == rangeBefore.getEnd());
    const auto tunedFile = tuned->getSourceFileReference().getFile();
    REQUIRE (tunedFile.existsAsFile());
    CHECK (tunedFile != srcWav);
    CHECK (tuned->getName().contains ("Tuned"));

    // RENDER-BEWEIS: die getunte Datei misst ≈ 440 Hz (A4), nicht mehr 445
    const float measured = detectFileMedianHz (tunedFile);
    CHECK (measured == Approx (440.0).margin (2.0));

    // EIN Undo stellt den Original-Clip (445-Quelle) wieder her
    CHECK (track->getClips().size() == 1);
    em.getUndoManager().undo();
    REQUIRE (track->getClips().size() == 1);
    auto* restored = dynamic_cast<te::WaveAudioClip*> (track->getClips()[0]);
    REQUIRE (restored != nullptr);
    CHECK (restored->getSourceFileReference().getFile() == srcWav);
}

TEST_CASE ("VocalTuner: guards — stretched clips and silence refuse politely", "[tune][p8]")
{
    EditManager em;
    em.createNewEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_tune_test");
    tempDir.createDirectory();

    // Stille: kein tonales Material → nullptr, Clip unangetastet
    juce::AudioBuffer<float> silence (1, (int) (1.5 * kSampleRate));
    silence.clear();
    const auto silentWav = writeWav (silence, tempDir, "silence.wav");

    auto* track = te::getAudioTracks (*em.getEdit())[0];
    auto* clip = clipEditor.importAudioFile (silentWav, *track, 1.0);
    REQUIRE (clip != nullptr);

    VocalTuner tuner (em);
    CHECK (tuner.tuneClipInPlace (*clip, {}) == nullptr);
    CHECK (track->getClips().size() == 1);
    CHECK (dynamic_cast<te::WaveAudioClip*> (track->getClips()[0])
               ->getSourceFileReference().getFile() == silentWav);
}

// ---------------------------------------------------------------------------
// Regression 2026-08-11: das Rendering knackte, sobald sich die Korrektur AENDERTE.
//
// User-Hoerurteil auf echtem Material war eindeutig und hat die Ursache
// eingegrenzt: Korrekturkurve konstant 0 -> sauber. Konstant +1 Halbton ->
// sauber, Tuning hoerbar. Die echte, sich aendernde Kurve -> "durchgehend
// Knacken und Verzerrung" — obwohl sie nur zwischen -0.45 und +0.45 Halbtoenen
// lag, also WENIGER verlangte als die saubere konstante Variante.
//
// Ursache: die alte Fassung rief setSpeedAndPitch pro Hop auf (86x/s).
// SoundTouch rechnet dabei Rate und Tempo neu und setzt interne Puffer zurueck
// — eine Stoerung im Millisekundentakt. Jetzt: konstante Tonhoehe je Abschnitt,
// ueberblendet.
//
// Kein Messwert der damaligen Testreihe zeigte das an (weder Cent-Abweichung
// noch Klick-Zaehlungen ueber erste oder zweite Ableitung). Die folgenden zwei
// Faelle pruefen deshalb, was maschinell tatsaechlich pruefbar ist: dass der
// Weg TRANSPARENT ist, wenn nichts zu korrigieren ist, und dass er zeitrichtig
// bleibt. Beides waere bei fehlerhafter Abschnitts-Ueberblendung sofort kaputt.
// ---------------------------------------------------------------------------
TEST_CASE ("VocalTuner: Korrektur 0 gibt das Original unveraendert zurueck", "[tune][P8]")
{
    constexpr double sr = 44100.0;
    const int n = (int) (2.0 * sr);

    // Gemischtes Signal: Grundton + Oberton + Amplitudenverlauf. Ein reiner
    // Sinus wuerde eine Fehlausrichtung der Abschnitte verzeihen.
    juce::AudioBuffer<float> in (1, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double env = 0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1.7 * t);
        in.setSample (0, i, (float) (env * (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                          + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 660.0 * t))));
    }

    juce::Array<float> zero;
    for (int i = 0; i < n / 512 + 2; ++i) zero.add (0.0f);

    juce::AudioBuffer<float> out;
    REQUIRE (VocalTuner::renderCorrected (in, out, sr, zero, 512));
    REQUIRE (out.getNumSamples() == n);

    // Pegel muss erhalten bleiben: eine falsche Ueberblendung wuerde an jeder
    // Abschnittsgrenze einen Einbruch erzeugen.
    const float rmsIn  = in .getRMSLevel (0, 0, n);
    const float rmsOut = out.getRMSLevel (0, 0, n);
    INFO ("RMS rein " << rmsIn << ", raus " << rmsOut);
    CHECK (rmsOut > rmsIn * 0.9f);
    CHECK (rmsOut < rmsIn * 1.1f);

    // Und es darf keine Stelle geben, an der das Signal einbricht — genau das
    // waere eine Luecke zwischen zwei Abschnitten.
    const int win = (int) (0.05 * sr);
    float quietest = 1.0e9f;
    for (int pos = 0; pos + win <= n; pos += win / 2)
        quietest = std::min (quietest, out.getRMSLevel (0, pos, win));
    const float quietestIn = [&]
    {
        float q = 1.0e9f;
        for (int pos = 0; pos + win <= n; pos += win / 2)
            q = std::min (q, in.getRMSLevel (0, pos, win));
        return q;
    }();
    INFO ("leisestes Fenster rein " << quietestIn << ", raus " << quietest);
    CHECK (quietest > quietestIn * 0.5f);
}

TEST_CASE ("VocalTuner: konstante Korrektur bleibt ueber die Laenge stabil", "[tune][P8]")
{
    constexpr double sr = 44100.0;
    const int n = (int) (2.0 * sr);

    juce::AudioBuffer<float> in (1, n);
    for (int i = 0; i < n; ++i)
        in.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                     * 220.0 * i / sr));

    juce::Array<float> up;
    for (int i = 0; i < n / 512 + 2; ++i) up.add (1.0f);      // +1 Halbton

    juce::AudioBuffer<float> out;
    REQUIRE (VocalTuner::renderCorrected (in, out, sr, up, 512));

    // Die Tonhoehe muss ueber die GANZE Laenge gleich bleiben — driftet die
    // Abschnitts-Ausrichtung, wandert sie.
    PitchDetector::Settings ps;
    auto measure = [&] (int from, int len)
    {
        return PitchDetector::medianPitchHz (
            PitchDetector::detectPitchHz (out.getReadPointer (0) + from, len, sr, ps));
    };
    const float expected = (float) VocalTuner::midiToHz (VocalTuner::hzToMidi (220.0) + 1.0);
    const float early = measure (0, n / 3);
    const float late  = measure (n * 2 / 3, n / 3);

    INFO ("erwartet " << expected << ", frueh " << early << ", spaet " << late);
    CHECK (std::abs (early - expected) < expected * 0.03f);
    CHECK (std::abs (late  - expected) < expected * 0.03f);
}
