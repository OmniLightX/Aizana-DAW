// test_clip_gain_envelope.cpp — P5 (S1-Parität): Multi-Point-Gain-Kurve pro Clip.
//
// Der wichtigste Case ist der RENDER-BEWEIS: TE muss die Automation-Kurve auf
// dem versteckten Clip-Volume-Plugin im Offline-Render auswerten — sonst wäre
// die ganze Mechanik ein Silent-Fail. Gerendert wird über bounceClipInPlace
// (nutzt denselben Renderer-Pfad wie die App).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/ClipGainEnvelope.h"

#include <cmath>

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr double kSourceSecs = 2.0;

    juce::File writeSineWav (const juce::File& dir, const juce::String& name)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) (kSourceSecs * kSampleRate));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * 440.0 * i / kSampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    // RMS of channel 0 within [fromSecs, toSecs) of an audio file
    float fileRmsRange (const juce::File& file, double fromSecs, double toSecs)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        const int from = (int) (fromSecs * reader->sampleRate);
        const int len  = (int) ((toSecs - fromSecs) * reader->sampleRate);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
        reader->read (&buf, 0, len, from, true, true);
        return buf.getRMSLevel (0, 0, len);
    }
}

TEST_CASE ("ClipGainEnvelope: points round-trip in dB and shift with the clip", "[gainenv][P5]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_gainenv_test");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 1.0);
    REQUIRE (clip != nullptr);

    CHECK_FALSE (ClipGainEnvelope::hasEnvelope (*clip));

    edit->getUndoManager().beginNewTransaction ("Gain-Punkt");
    ClipGainEnvelope::addPoint (*clip, 1.0, 0.0f);
    ClipGainEnvelope::addPoint (*clip, 2.0, -12.0f);

    REQUIRE (ClipGainEnvelope::hasEnvelope (*clip));
    REQUIRE (ClipGainEnvelope::numPoints (*clip) == 2);
    CHECK_THAT (ClipGainEnvelope::pointDb (*clip, 0), Catch::Matchers::WithinAbs (0.0f, 0.2f));
    CHECK_THAT (ClipGainEnvelope::pointDb (*clip, 1), Catch::Matchers::WithinAbs (-12.0f, 0.2f));
    CHECK_THAT (ClipGainEnvelope::pointTime (*clip, 1), Catch::Matchers::WithinAbs (2.0, 1e-6));

    // The carrier plugin is the internal "volume" type — invisible to user chains
    REQUIRE (ClipGainEnvelope::find (*clip) != nullptr);
    CHECK (ClipGainEnvelope::find (*clip)->getPluginType() == juce::String ("volume"));

    // Move the clip 0.5 s right — ClipEditor shifts the curve along
    edit->getUndoManager().beginNewTransaction ("Move");
    clipEditor.moveClip (*clip, 1.5);
    CHECK_THAT (ClipGainEnvelope::pointTime (*clip, 0), Catch::Matchers::WithinAbs (1.5, 1e-6));
    CHECK_THAT (ClipGainEnvelope::pointTime (*clip, 1), Catch::Matchers::WithinAbs (2.5, 1e-6));

    // One undo restores the previous point times
    edit->getUndoManager().undo();
    CHECK_THAT (ClipGainEnvelope::pointTime (*clip, 0), Catch::Matchers::WithinAbs (1.0, 1e-6));

    tempDir.deleteRecursively();
}

TEST_CASE ("ClipGainEnvelope: RENDER-BEWEIS — envelope is audible in the rendered file", "[gainenv][P5]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_gainenv_render");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 0.0);
    REQUIRE (clip != nullptr);

    // First half unity, second half -60 dB (sharp step around t = 1.0 s)
    edit->getUndoManager().beginNewTransaction ("Gain-Punkte");
    ClipGainEnvelope::addPoint (*clip, 0.00,  0.0f);
    ClipGainEnvelope::addPoint (*clip, 0.98,  0.0f);
    ClipGainEnvelope::addPoint (*clip, 1.02, -60.0f);
    ClipGainEnvelope::addPoint (*clip, 2.00, -60.0f);

    auto* bounced = clipEditor.bounceClipInPlace (*clip, true);
    REQUIRE (bounced != nullptr);

    const auto rendered = bounced->getSourceFileReference().getFile();
    REQUIRE (rendered.existsAsFile());

    const float rmsFirst  = fileRmsRange (rendered, 0.1, 0.9);
    const float rmsSecond = fileRmsRange (rendered, 1.1, 1.9);

    // Unity half carries the sine (~0.35 RMS); attenuated half is ~60 dB down.
    CHECK (rmsFirst > 0.2f);
    CHECK (rmsSecond < rmsFirst * 0.05f);   // > 26 dB Unterschied = Kurve wirkt

    tempDir.deleteRecursively();
}

TEST_CASE ("ClipGainEnvelope: split carries the envelope to the right half", "[gainenv][P5]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_gainenv_split");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 0.0);
    REQUIRE (clip != nullptr);

    edit->getUndoManager().beginNewTransaction ("Gain-Punkte");
    ClipGainEnvelope::addPoint (*clip, 0.0,  0.0f);
    ClipGainEnvelope::addPoint (*clip, 2.0, -12.0f);

    auto* right = clipEditor.cutClip (*clip, 1.0);
    REQUIRE (right != nullptr);

    // Both halves carry the curve; the gain profile over edit time is unchanged
    CHECK (ClipGainEnvelope::hasEnvelope (*clip));
    REQUIRE (ClipGainEnvelope::hasEnvelope (*right));
    CHECK_THAT (ClipGainEnvelope::gainDbAt (*right, 2.0),
                Catch::Matchers::WithinAbs (-12.0f, 0.3f));

    // The curve interpolates linearly in FADER position, not in dB — the
    // midpoint therefore sits at dB(avg(pos0, pos1)), not at -6 dB.
    const float expectedMidDb = te::volumeFaderPositionToDB (
        0.5f * (te::decibelsToVolumeFaderPosition (0.0f)
              + te::decibelsToVolumeFaderPosition (-12.0f)));
    CHECK_THAT (ClipGainEnvelope::gainDbAt (*right, 1.0),
                Catch::Matchers::WithinAbs (expectedMidDb, 0.5f));

    tempDir.deleteRecursively();
}
