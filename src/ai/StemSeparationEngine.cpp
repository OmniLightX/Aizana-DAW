#include "StemSeparationEngine.h"
#include "RoFormerStemBackend.h"
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>
#include <limits>

namespace
{
    struct TargetInfo { const char* instrument; const char* group; };
    const TargetInfo targets6[] = { { "Drums", "Drums" }, { "Bass", "Bass" },
        { "Other", "Synths" }, { "Vocals", "Vocals" }, { "Guitar", "Guitars" }, { "Piano", "Keys" } };
    constexpr const char* modelName = "mega53-five-masks-v1.onnx";
    constexpr const char* modelURL =
        "https://github.com/OmniLightX/Aizana-DAW/releases/download/stem-model-mega53-v1/mega53-five-masks-v1.onnx";

    bool writeStemWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        if (! stream) return false;
        // Floating-point stems preserve separation overshoots rather than clipping.
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), 44100.0, 2, 32, {}, 0));
        if (! writer) return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }
    void measureLevels (const juce::AudioBuffer<float>& audio, double& peakDb, double& rmsDb)
    {
        double peak = 0.0, squares = 0.0;
        const int n = audio.getNumSamples();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const double value = audio.getSample (c, i);
                peak = std::max (peak, std::abs (value));
                squares += value * value;
            }
        peakDb = juce::Decibels::gainToDecibels ((float) peak, -120.0f);
        rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (squares / std::max (1.0, 2.0 * n)), -120.0f);
    }
}

StemSeparationEngine::StemSeparationEngine() : juce::Thread ("StemSeparation") {}
StemSeparationEngine::~StemSeparationEngine()
{
    signalThreadShouldExit();
    // Never forcibly terminate an ONNX call with live allocations/session state.
    stopThread (-1);
}

juce::File StemSeparationEngine::modelsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Aizana").getChildFile ("Models");
}
juce::Array<juce::File> StemSeparationEngine::requiredModelFiles (Model)
{
    return { modelsDirectory().getChildFile (modelName) };
}
juce::Array<juce::URL> StemSeparationEngine::requiredModelURLs (Model)
{
    return { juce::URL (modelURL) };
}
bool StemSeparationEngine::isModelInstalled (Model model)
{
    const auto file = requiredModelFiles (model)[0];
    return file.existsAsFile() && file.getSize() == kModelBytes;
}
bool StemSeparationEngine::validateModelFile (const juce::File& file)
{
    return file.existsAsFile() && file.getSize() == kModelBytes
        && juce::SHA256 (file).toHexString() == kModelSha256;
}

bool StemSeparationEngine::loadAudioAs44kStereo (const juce::File& file, juce::AudioBuffer<float>& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0
        || reader->numChannels == 0 || reader->lengthInSamples > std::numeric_limits<int>::max() - 16)
        return false;
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> source (juce::jmin (2, (int) reader->numChannels), n);
    if (! reader->read (&source, 0, n, 0, true, true)) return false;
    juce::AudioBuffer<float> stereo (2, n + 8);
    stereo.clear(); // bounded interpolation lookahead at the file end
    for (int c = 0; c < 2; ++c)
        stereo.copyFrom (c, 0, source, std::min (c, source.getNumChannels() - 1), 0, n);
    if (std::abs (reader->sampleRate - kModelSampleRate) < 1.0)
    {
        stereo.setSize (2, n, true, false, true);
        out = std::move (stereo);
        return true;
    }
    const double ratio = reader->sampleRate / kModelSampleRate;
    const double length = std::floor (n / ratio);
    if (length <= 0 || length > std::numeric_limits<int>::max()) return false;
    out.setSize (2, (int) length);
    for (int c = 0; c < 2; ++c)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, stereo.getReadPointer (c), out.getWritePointer (c), out.getNumSamples());
    }
    return true;
}

bool StemSeparationEngine::startSeparation (const juce::File& song, const juce::File& outputDir,
                                            Model model, int threads)
{
    if (isBusy() || ! song.existsAsFile() || ! isModelInstalled (model)) return false;
    pendingSong = song;
    pendingOutDir = outputDir;
    pendingModel = model;
    pendingThreads = threads;
    return startThread();
}
void StemSeparationEngine::run()
{
    juce::Array<Stem> stems;
    auto progress = [this] (float value, juce::String stage)
    {
        juce::MessageManager::callAsync ([callback = onProgress, value, stage]
        { if (callback) callback (value, stage); });
    };
    const auto result = separateNow (pendingSong, pendingOutDir, pendingModel, pendingThreads, progress, stems);
    if (result.wasOk())
        juce::MessageManager::callAsync ([callback = onFinished, stems]
        { if (callback) callback (stems); });
    else
        juce::MessageManager::callAsync ([callback = onError, error = result.getErrorMessage()]
        { if (callback) callback (error); });
}

juce::Result StemSeparationEngine::separateNow (const juce::File& song, const juce::File& directory,
    Model model, int threads, std::function<void (float, juce::String)> progress, juce::Array<Stem>& stemsOut)
{
    stemsOut.clear();
    juce::Array<juce::File> created;
    bool complete = false;
    const auto cleanup = juce::ScopeGuard { [&]
    {
        if (! complete) for (const auto& file : created) file.deleteFile();
    } };
    auto report = [&] (float value, const juce::String& text) { if (progress) progress (value, text); };
    try
    {
        if (threadShouldExit()) return juce::Result::fail ("Abgebrochen");
        report (0.0f, juce::String::fromUTF8 ("Modell wird überprüft…"));
        const auto file = requiredModelFiles (model)[0];
        if (! validateModelFile (file))
        {
            // This dedicated cache filename is owned by this provider. A rejected
            // artifact must not keep passing the UI's fast installed-size check.
            file.deleteFile();
            return juce::Result::fail (juce::String::fromUTF8 (
                "Stem-Modell fehlt oder Prüfsumme stimmt nicht. Modell erneut herunterladen."));
        }
        juce::AudioBuffer<float> audio;
        report (0.02f, juce::String::fromUTF8 ("Audio wird geladen…"));
        if (! loadAudioAs44kStereo (song, audio)) return juce::Result::fail ("Audio-Datei nicht lesbar");
        std::vector<juce::AudioBuffer<float>> outputs;
        const auto result = RoFormerStemBackend::separate (file, audio, threads,
            [&] (float p) { report (0.05f + 0.89f * p, juce::String::fromUTF8 ("Song wird in Stems getrennt…")); },
            [this] { return threadShouldExit(); }, outputs);
        if (result.failed()) return result;
        const bool four = model == Model::fineTuned4Stem;
        if (four)
            for (int c = 0; c < 2; ++c)
            {
                outputs[2].addFrom (c, 0, outputs[4], c, 0, audio.getNumSamples());
                outputs[2].addFrom (c, 0, outputs[5], c, 0, audio.getNumSamples());
            }
        if (! directory.createDirectory()) return juce::Result::fail ("Ausgabeordner nicht erstellbar");
        juce::Array<Stem> staged;
        const int count = four ? 4 : 6;
        for (int i = 0; i < count; ++i)
        {
            if (threadShouldExit()) return juce::Result::fail ("Abgebrochen");
            Stem stem;
            stem.instrument = targets6[i].instrument;
            stem.group = targets6[i].group;
            measureLevels (outputs[(size_t) i], stem.peakDb, stem.rmsDb);
            stem.silent = stem.rmsDb < kSilenceRmsDb;
            if (! stem.silent)
            {
                // Never overwrite files referenced by a previous separation/import.
                stem.file = directory.getNonexistentChildFile (stem.instrument, ".wav");
                created.add (stem.file);
                if (! writeStemWav (stem.file, outputs[(size_t) i]))
                    return juce::Result::fail ("Stem nicht schreibbar: " + stem.instrument);
            }
            staged.add (stem);
            report (0.94f + 0.06f * (i + 1) / count, juce::String::fromUTF8 ("Stems werden geschrieben…"));
        }
        if (threadShouldExit()) return juce::Result::fail ("Abgebrochen");
        stemsOut = std::move (staged);
        complete = true;
        report (1.0f, "Fertig");
        return juce::Result::ok();
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail ("Stem-Trennung fehlgeschlagen: " + juce::String (e.what()));
    }
}
