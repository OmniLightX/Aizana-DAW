#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * TempoDetector — offline BPM estimation for an audio file (Phase 2+,
 * Studio One "Tempo-Detection" gap; simple DSP now, Essentia comes in Phase 3).
 *
 * Method: onset-strength envelope (rectified frame-energy flux, hop 512)
 * → autocorrelation over the lag range 60–200 BPM → peak pick → octave fold
 * into [70, 180). Works well on rhythmic material; it is an ESTIMATE and the
 * UI labels it as such.
 *
 * Threading: detectBpmInFile is pure and blocking — call it on a background
 * thread (the UI uses detectAsync, result delivered via callAsync). Never call
 * on the audio thread.
 */
class TempoDetector
{
public:
    /** Blocking analysis. Returns estimated BPM, or 0.0 when undetectable
        (silence, too short, unreadable file). Analyses at most the first 60 s. */
    static double detectBpmInFile (const juce::File& audioFile);

    /** Same analysis on an in-memory mono buffer (used by tests). */
    static double detectBpm (const juce::AudioBuffer<float>& mono, double sampleRate);

    /** Background-thread wrapper: runs detectBpmInFile on a detached worker and
        delivers the result on the message thread. The callback always fires
        (0.0 on failure) unless the app is shutting down. */
    static void detectAsync (const juce::File& audioFile,
                             std::function<void (double bpm)> onResult);
};
