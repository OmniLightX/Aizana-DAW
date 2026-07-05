// test_stem_separation.cpp — Phase 3: StemSeparationEngine (demucs.cpp wrapper)
//
// Real inference needs the ggml weights (53-324 MB, downloaded at runtime) and
// minutes of CPU — NOT part of the suite. These tests cover the headless-safe
// pieces: audio loading/resampling, model management, error paths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/StemSeparationEngine.h"

namespace
{
    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_sep_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        REQUIRE (dir.createDirectory());
        return dir;
    }

    juce::File writeSineWav (const juce::File& dir, const juce::String& name,
                             double sampleRate, int numChannels, double seconds)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, (unsigned) numChannels, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (numChannels, (int) (seconds * sampleRate));
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                               * 440.0 * i / sampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }
}

TEST_CASE ("StemSeparationEngine: loadAudioAs44kStereo resamples 48k and duplicates mono",
           "[separation]")
{
    auto dir = makeTempDir();

    // 48 kHz mono, 2 s → 44.1 kHz stereo, ~88200 samples
    auto f48 = writeSineWav (dir, "mono48.wav", 48000.0, 1, 2.0);
    juce::AudioBuffer<float> out;
    REQUIRE (StemSeparationEngine::loadAudioAs44kStereo (f48, out));
    CHECK (out.getNumChannels() == 2);
    CHECK (std::abs (out.getNumSamples() - (int) (2.0 * 44100.0)) < 64);
    // Both channels identical (mono duplicated)
    CHECK (out.getSample (0, 1000) == out.getSample (1, 1000));
    // Content survived resampling (sine still near full scale)
    CHECK (out.getMagnitude (0, 0, out.getNumSamples()) > 0.4f);

    // 44.1 kHz stereo passes through with identical length
    auto f44 = writeSineWav (dir, "stereo44.wav", 44100.0, 2, 1.0);
    juce::AudioBuffer<float> out44;
    REQUIRE (StemSeparationEngine::loadAudioAs44kStereo (f44, out44));
    CHECK (out44.getNumSamples() == 44100);

    // Unreadable input fails cleanly
    juce::AudioBuffer<float> none;
    CHECK_FALSE (StemSeparationEngine::loadAudioAs44kStereo (
        juce::File ("/nonexistent/x.wav"), none));

    dir.deleteRecursively();
}

TEST_CASE ("StemSeparationEngine: model management is consistent", "[separation]")
{
    using Model = StemSeparationEngine::Model;

    // File and URL lists pair up 1:1, and ft needs 4 files vs 1 for fast
    CHECK (StemSeparationEngine::requiredModelFiles (Model::fast6Stem).size() == 1);
    CHECK (StemSeparationEngine::requiredModelFiles (Model::fineTuned4Stem).size() == 4);
    CHECK (StemSeparationEngine::requiredModelURLs (Model::fast6Stem).size() == 1);
    CHECK (StemSeparationEngine::requiredModelURLs (Model::fineTuned4Stem).size() == 4);

    for (const auto& f : StemSeparationEngine::requiredModelFiles (Model::fineTuned4Stem))
        CHECK (f.getFullPathName().contains ("Models"));
}

TEST_CASE ("StemSeparationEngine: separateNow fails cleanly without a model", "[separation]")
{
    // Guard: skip if the developer machine actually has the weights installed.
    if (StemSeparationEngine::isModelInstalled (StemSeparationEngine::Model::fineTuned4Stem))
        SUCCEED ("ft model installed on this machine — error path not testable");
    else
    {
        auto dir = makeTempDir();
        auto song = writeSineWav (dir, "song.wav", 44100.0, 2, 1.0);

        StemSeparationEngine engine;
        juce::Array<StemSeparationEngine::Stem> stems;
        const auto result = engine.separateNow (song, dir.getChildFile ("out"),
                                                StemSeparationEngine::Model::fineTuned4Stem,
                                                2, nullptr, stems);
        CHECK (result.failed());
        CHECK (stems.isEmpty());

        // Async API refuses to start without a model
        CHECK_FALSE (engine.startSeparation (song, dir.getChildFile ("out"),
                                             StemSeparationEngine::Model::fineTuned4Stem));
        dir.deleteRecursively();
    }
}
