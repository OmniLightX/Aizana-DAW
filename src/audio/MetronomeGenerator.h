#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

/**
 * MetronomeGenerator — Audio-thread-safe beat click synthesizer.
 *
 * Produces a short sine-burst click on every beat, with an accented downbeat
 * (beat 1 of each bar): higher pitch AND louder than the off-beats so the bar
 * boundary is clearly audible. Parameters are communicated via std::atomic<> —
 * no locks, no allocations on the audio thread.
 *
 * Audio thread invariants: ALL state that changes across threads uses std::atomic.
 * The processBlock() method MUST NOT allocate memory or call any non-RT-safe API.
 */
class MetronomeGenerator
{
public:
    MetronomeGenerator() = default;

    // -----------------------------------------------------------------------
    // Called from message thread (safe — atomics)
    // -----------------------------------------------------------------------

    void setEnabled (bool enabled) noexcept { isEnabled.store (enabled); }
    bool getEnabled() const noexcept         { return isEnabled.load(); }

    void setBpm (double bpm) noexcept
    {
        currentBpm.store (juce::jlimit (20.0, 300.0, bpm));
    }

    void setBeatsPerBar (int bpb) noexcept
    {
        beatsPerBar.store (juce::jlimit (1, 16, bpb));
    }

    // -----------------------------------------------------------------------
    // Called from audio thread
    // -----------------------------------------------------------------------

    /** Must be called before processBlock. Runs on message thread (device init). */
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        // Pre-allocate click envelope buffer at max block size
        // Click duration: 10ms — about 441 samples at 44.1kHz (tighter, punchier click)
        clickDurationSamples = (int)(sampleRate * 0.010);
        reset();
    }

    /** Mix click audio into outputData. Called on audio thread. */
    void processBlock (float* const* outputData, int numChannels, int numSamples) noexcept;

private:
    std::atomic<bool>   isEnabled     { false };
    std::atomic<double> currentBpm    { 120.0 };
    std::atomic<int>    beatsPerBar   { 4 };

    double sr = 44100.0;
    int    clickDurationSamples = 441;   // updated in prepare() — 10ms at 44.1kHz

    // Audio-thread state (single-threaded — audio callback only)
    double phaseAccumulator    = 0.0;   // position within current beat [0.0, 1.0)
    int    beatInBar           = 0;     // which beat in the bar (0 = downbeat)
    int    clickSamplesLeft    = 0;     // samples remaining in current click burst
    double clickPhase          = 0.0;   // sine oscillator phase for click tone
    double clickFreq           = 1200.0; // frequency locked in at beat boundary, persists for full click
    double clickAmp            = 0.34;   // peak amplitude locked at beat boundary — louder on the downbeat (accent)

    void reset() noexcept
    {
        phaseAccumulator  = 0.0;
        beatInBar         = 0;
        clickSamplesLeft  = 0;
        clickPhase        = 0.0;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeGenerator)
};
