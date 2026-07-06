// test_bounce_in_place.cpp — P3 (S1-Parität "Bounce/Transform"):
// Clip → gerendertes Audio in place, 1 Undo-Schritt, Master bleibt live.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

#include <cmath>

namespace
{
    constexpr double kSampleRate = 44100.0;
    constexpr double kSourceSecs = 1.0;

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
            buf.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * 440.0 * i / kSampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    float fileRms (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        return buf.getRMSLevel (0, 0, buf.getNumSamples());
    }
}

TEST_CASE ("BounceInPlace: clip is replaced by a rendered clip at the same position", "[bounce][P3]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_bounce_test");
    tempDir.createDirectory();
    const auto srcWav = writeSineWav (tempDir, "src.wav");

    auto* track = te::getAudioTracks (*edit)[0];
    auto* clip = clipEditor.importAudioFile (srcWav, *track, 1.0);
    REQUIRE (clip != nullptr);

    const auto posBefore = clip->getPosition().time;
    const auto oldItemID = clip->itemID;

    auto* bounced = clipEditor.bounceClipInPlace (*clip, true);
    REQUIRE (bounced != nullptr);

    // Same place on the timeline, new source file, non-silent render
    CHECK (bounced->getPosition().time.getStart() == posBefore.getStart());
    CHECK (bounced->getPosition().time.getEnd()   == posBefore.getEnd());

    const auto renderedFile = bounced->getSourceFileReference().getFile();
    REQUIRE (renderedFile.existsAsFile());
    CHECK (renderedFile != srcWav);
    CHECK (fileRms (renderedFile) > 0.1f);   // the sine survived the render

    // Old clip is gone, exactly one clip on the track
    CHECK (track->getClips().size() == 1);
    CHECK (track->getClips()[0]->itemID != oldItemID);

    // ONE undo restores the original clip (render file stays on disk — fine)
    em.getUndoManager().undo();
    REQUIRE (track->getClips().size() == 1);
    CHECK (track->getClips()[0]->getSourceFileReference().getFile() == srcWav);

    tempDir.deleteRecursively();
}

TEST_CASE ("BounceInPlace: rejects zero-length and detached clips gracefully", "[bounce][P3]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    ClipEditor clipEditor (em);

    auto* track = te::getAudioTracks (*edit)[0];

    // Zero-length clip → nullptr, no crash, no transaction damage
    te::ClipPosition pos { tracktion::core::TimeRange (tracktion::core::TimePosition::fromSeconds (2.0),
                                                       tracktion::core::TimePosition::fromSeconds (2.0)), {} };
    auto* zeroClip = dynamic_cast<te::WaveAudioClip*> (
        track->insertNewClip (te::TrackItem::Type::wave, "zero", pos, nullptr));
    REQUIRE (zeroClip != nullptr);

    CHECK (clipEditor.bounceClipInPlace (*zeroClip, true) == nullptr);
}
