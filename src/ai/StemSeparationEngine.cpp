#include "StemSeparationEngine.h"

#include <juce_events/juce_events.h>

// Vendored demucs.cpp inference (extern/demucs_cpp, MIT) — heavy Eigen headers,
// keep them OUT of our own header.
#include "model.hpp"
#include "threaded_inference.hpp"

namespace
{
    struct TargetInfo { const char* instrument; const char* group; };

    // Target order is fixed by the model architecture (see demucs.cpp cli-apps).
    // "Other" maps to the Synths group: after 6-stem separation the residual is
    // overwhelmingly synths/pads in practice; the Pro preview lets users rebin.
    const TargetInfo kTargets6[] = { { "Drums", "Drums" },  { "Bass", "Bass" },
                                     { "Other", "Synths" }, { "Vocals", "Vocals" },
                                     { "Guitar", "Guitars" }, { "Piano", "Keys" } };
    const TargetInfo kTargets4[] = { { "Drums", "Drums" },  { "Bass", "Bass" },
                                     { "Other", "Synths" }, { "Vocals", "Vocals" } };

    const char* kFt4FileNames[] = { "ggml-model-htdemucs_ft_drums-4s-f16.bin",
                                    "ggml-model-htdemucs_ft_bass-4s-f16.bin",
                                    "ggml-model-htdemucs_ft_other-4s-f16.bin",
                                    "ggml-model-htdemucs_ft_vocals-4s-f16.bin" };
    const char* k6sFileName     = "ggml-model-htdemucs-6s-f16.bin";
    const char* kWeightsBaseUrl =
        "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/";

    bool writeStemWav (const juce::File& file, const Eigen::Tensor3dXf& targets,
                       int targetIndex, double sampleRate)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release();

        const int n = (int) targets.dimension (2);
        juce::AudioBuffer<float> buf (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample (ch, i, targets ((long) targetIndex, (long) ch, (long) i));

        return writer->writeFromAudioSampleBuffer (buf, 0, n);
    }

    void measureLevels (const Eigen::Tensor3dXf& targets, int targetIndex,
                        double& peakDb, double& rmsDb)
    {
        const int n = (int) targets.dimension (2);
        double peak = 0.0, sumSq = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const double v = std::abs ((double) targets ((long) targetIndex,
                                                             (long) ch, (long) i));
                peak = std::max (peak, v);
                sumSq += v * v;
            }
        const double rms = std::sqrt (sumSq / std::max (1, 2 * n));
        peakDb = juce::Decibels::gainToDecibels ((float) peak, -120.0f);
        rmsDb  = juce::Decibels::gainToDecibels ((float) rms,  -120.0f);
    }
}

//==============================================================================
StemSeparationEngine::StemSeparationEngine() : juce::Thread ("StemSeparation") {}

StemSeparationEngine::~StemSeparationEngine()
{
    // Inference segments cannot be interrupted — give the thread a moment, then
    // let process teardown reap it (only stems-in-progress are lost, no state).
    signalThreadShouldExit();
    stopThread (2000);
}

//==============================================================================
juce::File StemSeparationEngine::modelsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Cloudes").getChildFile ("Models");
}

juce::Array<juce::File> StemSeparationEngine::requiredModelFiles (Model m)
{
    juce::Array<juce::File> files;
    if (m == Model::fast6Stem)
        files.add (modelsDirectory().getChildFile (k6sFileName));
    else
        for (auto* name : kFt4FileNames)
            files.add (modelsDirectory().getChildFile (name));
    return files;
}

juce::Array<juce::URL> StemSeparationEngine::requiredModelURLs (Model m)
{
    juce::Array<juce::URL> urls;
    if (m == Model::fast6Stem)
        urls.add (juce::URL (juce::String (kWeightsBaseUrl) + k6sFileName));
    else
        for (auto* name : kFt4FileNames)
            urls.add (juce::URL (juce::String (kWeightsBaseUrl) + name));
    return urls;
}

bool StemSeparationEngine::isModelInstalled (Model m)
{
    for (const auto& f : requiredModelFiles (m))
        if (! f.existsAsFile() || f.getSize() < 1024 * 1024)
            return false;
    return true;
}

//==============================================================================
bool StemSeparationEngine::loadAudioAs44kStereo (const juce::File& file,
                                                 juce::AudioBuffer<float>& out)
{
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    const int srcLen = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> src (juce::jmin (2, (int) reader->numChannels), srcLen);
    reader->read (&src, 0, srcLen, 0, true, true);

    // Mono → duplicated stereo (extra channels beyond 2 are dropped by the read)
    juce::AudioBuffer<float> stereo (2, srcLen);
    for (int ch = 0; ch < 2; ++ch)
        stereo.copyFrom (ch, 0, src, juce::jmin (ch, src.getNumChannels() - 1), 0, srcLen);

    if (std::abs (reader->sampleRate - kModelSampleRate) < 1.0)
    {
        out = std::move (stereo);
        return true;
    }

    // Resample to the model's mandatory 44.1 kHz (sessions are often 48 kHz).
    const double ratio  = reader->sampleRate / kModelSampleRate;
    const int    outLen = (int) std::floor (srcLen / ratio);
    if (outLen < 1024)
        return false;

    out.setSize (2, outLen);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, stereo.getReadPointer (ch), out.getWritePointer (ch), outLen);
    }
    return true;
}

//==============================================================================
bool StemSeparationEngine::startSeparation (const juce::File& song,
                                            const juce::File& outputDir,
                                            Model model, int numThreads)
{
    if (isBusy() || ! song.existsAsFile() || ! isModelInstalled (model))
        return false;

    pendingSong    = song;
    pendingOutDir  = outputDir;
    pendingModel   = model;
    pendingThreads = numThreads;
    startThread();
    return true;
}

void StemSeparationEngine::run()
{
    juce::Array<Stem> stems;

    auto marshalProgress = [this] (float p, juce::String stage)
    {
        juce::MessageManager::callAsync ([cb = onProgress, p, stage]()
        {
            if (cb) cb (p, stage);
        });
    };

    const auto result = separateNow (pendingSong, pendingOutDir, pendingModel,
                                     pendingThreads, marshalProgress, stems);

    if (result.wasOk())
        juce::MessageManager::callAsync ([cb = onFinished, stems]()
        {
            if (cb) cb (stems);
        });
    else
        juce::MessageManager::callAsync ([cb = onError, msg = result.getErrorMessage()]()
        {
            if (cb) cb (msg);
        });
}

//==============================================================================
juce::Result StemSeparationEngine::separateNow (
    const juce::File& song, const juce::File& outputDir, Model model,
    int numThreads, std::function<void (float, juce::String)> progress,
    juce::Array<Stem>& stemsOut)
{
    auto report = [&progress] (float p, const juce::String& stage)
    {
        if (progress) progress (p, stage);
    };

    if (! isModelInstalled (model))
        return juce::Result::fail ("KI-Modell nicht installiert");
    if (! outputDir.createDirectory())
        return juce::Result::fail ("Ausgabeordner nicht erstellbar");

    report (0.0f, juce::String::fromUTF8 ("Audio wird geladen…"));
    juce::AudioBuffer<float> audio;
    if (! loadAudioAs44kStereo (song, audio))
        return juce::Result::fail ("Audio-Datei nicht lesbar");

    Eigen::MatrixXf eigenAudio (2, audio.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            eigenAudio (ch, i) = audio.getSample (ch, i);
    audio.setSize (0, 0);   // free the JUCE copy before inference allocates

    const auto modelFiles = requiredModelFiles (model);
    const bool isFt       = model == Model::fineTuned4Stem;
    const int  numTargets = isFt ? 4 : 6;
    const auto* targets   = isFt ? kTargets4 : kTargets6;

    // ft: each of the 4 fine-tuned models contributes its specialty target.
    // 6s: a single model produces all 6 targets in one inference pass.
    const int numRuns = isFt ? 4 : 1;
    Eigen::Tensor3dXf output;

    for (int runIdx = 0; runIdx < numRuns; ++runIdx)
    {
        if (threadShouldExit())
            return juce::Result::fail ("Abgebrochen");

        report (numRuns == 1 ? 0.05f : 0.05f + 0.9f * runIdx / numRuns,
                juce::String::fromUTF8 ("KI-Modell wird geladen…"));

        demucscpp::demucs_model demucsModel;
        if (! demucscpp::load_demucs_model (
                modelFiles[runIdx].getFullPathName().toStdString(), &demucsModel))
            return juce::Result::fail ("KI-Modell nicht ladbar: "
                                       + modelFiles[runIdx].getFileName());

        const juce::String stage = isFt
            ? juce::String::fromUTF8 ("Trenne ") + targets[runIdx].instrument
              + " (" + juce::String (runIdx + 1) + "/4)…"
            : juce::String::fromUTF8 ("Song wird in Stems getrennt…");

        auto inferenceCb = [&report, runIdx, numRuns, stage] (float p, const std::string&)
        {
            report (0.05f + 0.9f * (runIdx + p) / (float) numRuns, stage);
        };

        auto runOut = demucscppthreaded::threaded_inference (
            demucsModel, eigenAudio, numThreads, "", inferenceCb);

        if (isFt)
        {
            // Keep only this model's specialty target row.
            if (runIdx == 0)
            {
                output = Eigen::Tensor3dXf (4, runOut.dimension (1), runOut.dimension (2));
                output.setZero();
            }
            output.chip (runIdx, 0) = runOut.chip (runIdx, 0);
        }
        else
        {
            output = std::move (runOut);
        }
    }

    if (threadShouldExit())
        return juce::Result::fail ("Abgebrochen");

    report (0.96f, juce::String::fromUTF8 ("Stems werden geschrieben…"));

    for (int t = 0; t < numTargets; ++t)
    {
        Stem stem;
        stem.instrument = targets[t].instrument;
        stem.group      = targets[t].group;
        measureLevels (output, t, stem.peakDb, stem.rmsDb);
        stem.silent = stem.rmsDb < kSilenceRmsDb;

        if (! stem.silent)
        {
            stem.file = outputDir.getChildFile (stem.instrument + ".wav");
            if (! writeStemWav (stem.file, output, t, kModelSampleRate))
                return juce::Result::fail ("Stem nicht schreibbar: " + stem.instrument);
        }

        stemsOut.add (stem);
    }

    report (1.0f, "Fertig");
    return juce::Result::ok();
}
