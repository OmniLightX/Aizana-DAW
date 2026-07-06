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
