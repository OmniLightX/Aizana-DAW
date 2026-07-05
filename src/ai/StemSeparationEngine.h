#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/**
 * StemSeparationEngine — Phase 3: full song → stems (demucs.cpp, Spike S1 GO).
 *
 * Wraps the vendored demucs.cpp inference library (extern/demucs_cpp, MIT) for
 * Aizana: loads any audio file via JUCE, resamples to the model's mandatory
 * 44.1 kHz stereo, runs the hybrid-transformer separation on a background
 * thread, writes per-instrument WAVs (24-bit/44.1k — TE resamples on playback),
 * and skips near-silent stems (e.g. "Guitar" in a song without guitar).
 *
 * Models (ggml weights, downloaded at runtime — never bundled):
 *   fast6Stem     htdemucs-6s    1 file,  53 MB — Drums/Bass/Other/Vocals/
 *                 Guitar/Piano. Measured ~1.7x audio duration on M3 (4 threads).
 *   fineTuned4Stem htdemucs_ft   4 files, 324 MB — Drums/Bass/Other/Vocals,
 *                 noticeably less bleed. ~7.5x audio duration (runs 4 models).
 *
 * THREADING: startSeparation() runs on a dedicated background thread; the
 * std::function callbacks fire on the MESSAGE thread (marshalled internally).
 * separateNow() is the synchronous core for tests/workers — its callbacks fire
 * on the CALLING thread. Never call either from the audio thread.
 *
 * Cancellation: takes effect between pipeline stages (load → infer → write;
 * between the 4 ft models). A running inference segment cannot be aborted.
 */
class StemSeparationEngine : private juce::Thread
{
public:
    enum class Model { fast6Stem, fineTuned4Stem };

    struct Stem
    {
        juce::String instrument;   // "Drums", "Bass", "Vocals", "Other", …
        juce::String group;        // Aizana mixer group ("Drums", …, "Synths")
        juce::File   file;         // written WAV — invalid if silent
        double peakDb = -120.0, rmsDb = -120.0;
        bool   silent = false;     // below kSilenceRmsDb → no file written
    };

    StemSeparationEngine();
    ~StemSeparationEngine() override;

    // ---- model management -------------------------------------------------
    static juce::File modelsDirectory();                       // App Support/Aizana/Models
    static juce::Array<juce::File> requiredModelFiles (Model); // expected local files
    static juce::Array<juce::URL>  requiredModelURLs  (Model); // download sources
    static bool isModelInstalled (Model);

    // ---- async API (message-thread callbacks) ------------------------------
    std::function<void (float progress01, juce::String stage)> onProgress;
    std::function<void (juce::Array<Stem>)>                    onFinished;
    std::function<void (juce::String error)>                   onError;

    /** Returns false (and does nothing) if busy, the file is missing or the
        model is not installed — callers handle the download flow first. */
    bool startSeparation (const juce::File& song, const juce::File& outputDir,
                          Model model, int numThreads = 4);

    bool isBusy() const noexcept { return isThreadRunning(); }

    /** Request cancel — honoured at the next stage boundary. */
    void cancelSeparation() { signalThreadShouldExit(); }

    // ---- synchronous core (tests / worker threads) -------------------------
    juce::Result separateNow (const juce::File& song, const juce::File& outputDir,
                              Model model, int numThreads,
                              std::function<void (float, juce::String)> progress,
                              juce::Array<Stem>& stemsOut);

    // ---- exposed for tests --------------------------------------------------
    /** Load any readable audio file as 44.1 kHz stereo (mono duplicated,
        extra channels dropped). Returns false on unreadable input. */
    static bool loadAudioAs44kStereo (const juce::File&, juce::AudioBuffer<float>& out);

    static constexpr double kSilenceRmsDb   = -60.0;
    static constexpr double kModelSampleRate = 44100.0;

private:
    void run() override;

    juce::File pendingSong, pendingOutDir;
    Model      pendingModel  = Model::fast6Stem;
    int        pendingThreads = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemSeparationEngine)
};
