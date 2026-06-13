#pragma once

#include "ReferenceProfile.h"
#include <juce_audio_formats/juce_audio_formats.h>

/**
 * ReferenceAnalyzer — Phase 3, Iteration 2 (REF-01).
 *
 * Measures a reference track's sonic character and turns it into a
 * ReferenceProfile: integrated loudness (real EBU R128 via libebur128, same
 * library as the master meter) plus the relative low/mid/high band balance
 * from averaged FFT spectra.
 *
 * THREAD: reads the whole file + runs FFTs — background thread only, never
 * the audio thread. analyzeAsync marshals its callback to the message thread.
 */
class ReferenceAnalyzer
{
public:
    /** Synchronous analysis (background/test threads). Returns an invalid
        profile (name empty) if the file is unreadable. */
    static ReferenceProfile analyzeFile (const juce::File&);

    /** Async wrapper: worker thread + message-thread callback. */
    static void analyzeAsync (const juce::File&,
                              std::function<void (ReferenceProfile)> onDone);

    /** Exposed for tests: band balance from a mono buffer (relative dB around
        the mean — low 20-250 Hz, mid 250-4k, high 4k-20k). */
    static void measureBandBalance (const float* mono, int numSamples, double sampleRate,
                                    double& lowDb, double& midDb, double& highDb);

    /** Exposed for tests: vocal-presence emphasis (1-4 kHz band vs the rest,
        clamped ±3 dB). Heuristic — a bright lead synth reads as presence too;
        a real vocal-detection model is Phase 4 (IInferenceBackend). */
    static double measureVocalPresence (const float* mono, int numSamples, double sampleRate);
};
