#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/**
 * StemAnalyzer — Phase 3, Iteration 1, Slice 1 (AI-01).
 *
 * Classifies imported stem files into instruments and mixer groups using a
 * two-tier strategy (decision E2, 2026-06-12: heuristics-first, no ML dep):
 *
 *   Tier 1 — filename/keyword heuristics (DE/EN). Real-world stems are named
 *            ("Kick In", "Vocal Mic", "Gtr DI"), so this covers most imports
 *            instantly and is fully deterministic/testable.
 *   Tier 2 — audio-feature fallback for unnamed/ambiguous files: onset rate +
 *            spectral centroid + level stats. Deliberately conservative —
 *            "Other" with low confidence beats a wrong guess (the user can
 *            correct in the Pro preview table / via undo).
 *
 * Tier 2 is replaced by a real classifier behind IInferenceBackend in Slice 5
 * without touching callers.
 *
 * THREAD: analyzeFile/analyzeFiles do file I/O + FFT — call from a background
 * thread (AIEngine pool), NEVER the audio thread. Results are plain values.
 */
struct StemInfo
{
    juce::File   file;
    juce::String instrument;       // display label → track name ("Kick", "Vocals", …)
    juce::String group;            // mixer group → named bus ("Drums", "Bass", "Guitars",
                                   //                          "Vocals", "Keys", "FX", "Other")
    double       confidence  = 0.0;   // 0..1
    bool         fromFilename = false;
    double       peakDb = -120.0;     // for gain staging (AI-03)
    double       rmsDb  = -120.0;
};

class StemAnalyzer
{
public:
    /** Full analysis: filename tier first, audio tier as fallback, level stats always. */
    static StemInfo analyzeFile (const juce::File& file);

    /** Convenience: analyse a set of files (e.g. one drag-drop batch). */
    static juce::Array<StemInfo> analyzeFiles (const juce::Array<juce::File>& files);

    /** Tier 1 only — exposed for tests. Returns false if no keyword matched. */
    static bool classifyFromName (const juce::String& fileNameWithoutExtension,
                                  juce::String& instrumentOut,
                                  juce::String& groupOut,
                                  double& confidenceOut);

    /** Tier 2 only — exposed for tests. Mono audio in, classification out.
        Always succeeds (worst case: "Other" with low confidence). */
    static void classifyFromAudio (const float* mono, int numSamples, double sampleRate,
                                   juce::String& instrumentOut,
                                   juce::String& groupOut,
                                   double& confidenceOut);
};
