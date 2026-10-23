#include <catch2/catch_test_macros.hpp>
#include "ai/RoFormerStemBackend.h"
#include "ai/StemSeparationEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

TEST_CASE ("Stem spectral transform preserves short stereo audio and boundaries", "[separation][spectral]")
{
    for (int length : { 1, 17, 511, 1024, 4097, 88200 })
    {
        juce::AudioBuffer<float> input (2, length);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                input.setSample (c, i, 0.2f * std::sin (0.013f * i + c));
        input.setSample (0, 0, 0.7f);
        input.setSample (1, length - 1, -0.4f);
        auto encoded = StemSpectralTransform::encode (input);
        REQUIRE (encoded.samples == length);
        REQUIRE (encoded.frames == 1 + length / 512);
        auto decoded = StemSpectralTransform::decode (encoded);
        REQUIRE (decoded.getNumChannels() == 2);
        REQUIRE (decoded.getNumSamples() == length);
        float maxError = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                maxError = std::max (maxError, std::abs (decoded.getSample (c, i) - input.getSample (c, i)));
        CHECK (maxError < 2.0e-6f);
    }
}

TEST_CASE ("Stem backend rejects missing model without publishing outputs", "[separation]")
{
    juce::AudioBuffer<float> audio (2, 4410);
    audio.clear();
    std::vector<juce::AudioBuffer<float>> stems;
    auto result = RoFormerStemBackend::separate (juce::File ("/nonexistent/aizana-model.onnx"),
                                                audio, 2, {}, {}, stems);
    CHECK (result.failed());
    CHECK (stems.empty());
}

TEST_CASE ("Stem backend honours cancellation before model loading", "[separation]")
{
    juce::AudioBuffer<float> audio (2, 4410);
    audio.clear();
    std::vector<juce::AudioBuffer<float>> stems;
    auto result = RoFormerStemBackend::separate (juce::File ("/nonexistent/aizana-model.onnx"),
                                                audio, 2, {}, [] { return true; }, stems);
    CHECK (result.failed());
    CHECK (result.getErrorMessage() == "Abgebrochen");
    CHECK (stems.empty());
}

TEST_CASE ("Native stem waveforms match the independent ONNX reference", "[.stem-model]")
{
    const auto modelPath = juce::SystemStats::getEnvironmentVariable ("AIZANA_STEM_TEST_MODEL", {});
    const auto referencePath = juce::SystemStats::getEnvironmentVariable ("AIZANA_STEM_PARITY_DIR", {});
    if (modelPath.isEmpty() || referencePath.isEmpty()) SKIP ("Requires local qualified model and private reference directory");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto read = [&] (const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        REQUIRE (reader->sampleRate == 44100.0);
        REQUIRE (reader->numChannels == 2);
        juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
        REQUIRE (reader->read (&audio, 0, audio.getNumSamples(), 0, true, true));
        return audio;
    };
    REQUIRE (StemSeparationEngine::validateModelFile (juce::File (modelPath)));
    const juce::File reference (referencePath);
    const auto input = read (reference.getChildFile ("input.wav"));
    std::vector<juce::AudioBuffer<float>> outputs;
    const auto inference = RoFormerStemBackend::separate (juce::File (modelPath), input, 4, {}, {}, outputs);
    INFO (inference.getErrorMessage().toStdString());
    REQUIRE (inference.wasOk());
    REQUIRE (outputs.size() == 6);
    const char* names[] = { "Drums", "Bass", "Other", "Vocals", "Guitar", "Piano" };
    for (int stem = 0; stem < 6; ++stem)
    {
        const auto expected = read (reference.getChildFile (juce::String (names[stem]) + ".wav"));
        REQUIRE (outputs[(size_t) stem].getNumSamples() == input.getNumSamples());
        REQUIRE (expected.getNumSamples() == input.getNumSamples());
        double squares = 0.0;
        float maxError = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < input.getNumSamples(); ++i)
            {
                const auto error = outputs[(size_t) stem].getSample (c, i) - expected.getSample (c, i);
                maxError = std::max (maxError, std::abs (error));
                squares += (double) error * error;
            }
        INFO ("Category " << names[stem] << ", max error " << maxError);
        CHECK (maxError < 2.0e-4f);
        CHECK (std::sqrt (squares / (2.0 * input.getNumSamples())) < 1.0e-5);
    }
    float sumError = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < input.getNumSamples(); ++i)
        {
            float sum = 0.0f;
            for (const auto& stem : outputs) sum += stem.getSample (c, i);
            sumError = std::max (sumError, std::abs (sum - input.getSample (c, i)));
        }
    CHECK (sumError < 1.0e-6f);
}

TEST_CASE ("Stem engine writes both output modes without overwriting and cleans cancellation", "[.stem-model-engine]")
{
    using Model = StemSeparationEngine::Model;
    const auto inputPath = juce::SystemStats::getEnvironmentVariable ("AIZANA_STEM_PARITY_DIR", {});
    if (inputPath.isEmpty() || ! StemSeparationEngine::isModelInstalled (Model::fast6Stem))
        SKIP ("Requires installed qualified model and local reference input");
    const juce::File input = juce::File (inputPath).getChildFile ("input.wav");
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("aizana_stem_engine", {});
    REQUIRE (directory.createDirectory());
    const auto cleanup = juce::ScopeGuard { [&] { directory.deleteRecursively(); } };
    auto sentinel = directory.getChildFile ("Drums.wav");
    REQUIRE (sentinel.replaceWithText ("existing stem must survive"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (auto mode : { Model::fast6Stem, Model::fineTuned4Stem })
    {
        StemSeparationEngine engine;
        juce::Array<StemSeparationEngine::Stem> stems;
        const auto result = engine.separateNow (input, directory, mode, 4, {}, stems);
        INFO (result.getErrorMessage().toStdString());
        REQUIRE (result.wasOk());
        REQUIRE (stems.size() == (mode == Model::fast6Stem ? 6 : 4));
        CHECK (sentinel.loadFileAsString() == "existing stem must survive");
        for (const auto& stem : stems)
        {
            if (stem.silent) continue;
            REQUIRE (stem.file != sentinel);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (stem.file));
            REQUIRE (reader != nullptr);
            CHECK (reader->usesFloatingPointData);
            CHECK (reader->bitsPerSample == 32);
            CHECK (reader->sampleRate == 44100.0);
            CHECK (reader->numChannels == 2);
            CHECK (reader->lengthInSamples == 352800);
        }
    }
    const auto filesBefore = directory.findChildFiles (juce::File::findFiles, false).size();
    StemSeparationEngine cancelled;
    juce::Array<StemSeparationEngine::Stem> outputs;
    bool reachedWrite = false;
    const auto result = cancelled.separateNow (input, directory, Model::fast6Stem, 4,
        [&] (float progress, juce::String)
        {
            if (progress > 0.94f && progress < 1.0f)
            {
                reachedWrite = true;
                cancelled.cancelSeparation();
            }
        }, outputs);
    REQUIRE (reachedWrite);
    CHECK (result.failed());
    CHECK (outputs.isEmpty());
    CHECK (directory.findChildFiles (juce::File::findFiles, false).size() == filesBefore);
    CHECK (sentinel.loadFileAsString() == "existing stem must survive");
}
