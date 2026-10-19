#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/** Local Mega 53 stem separation (original weights explicitly MIT-licensed).
 * Five trained instrument estimates plus a residual Other produce six categories.
 * The four-category mode folds Guitar and Piano into Other; it is not a separately
 * fine-tuned model. Legacy enum names are retained for source compatibility.
 * Downloaded model weights are not part of the source repository.
 *
 * All inference and file I/O run on the dedicated worker. Async callbacks are
 * marshalled to the message thread. separateNow() is synchronous for workers and
 * tests only. Cancellation is honoured between bounded four-second chunks; worker
 * shutdown joins safely rather than terminating a running inference thread.
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
    static juce::File modelsDirectory();                       // JUCE userApplicationDataDirectory/Aizana/Models
    static juce::Array<juce::File> requiredModelFiles (Model); // expected local files
    static juce::Array<juce::URL>  requiredModelURLs  (Model); // download sources
    static bool isModelInstalled (Model); // fast size/path check, no UI-thread hashing
    static bool validateModelFile (const juce::File&); // full SHA-256 check; worker only
    static constexpr juce::int64 kModelBytes = 355012879;
    static constexpr const char* kModelSha256 =
        "d546b9780159ee8e54c3cf7c0446a4204b8793e363941122b4c115089d369cc2";

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
