#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/**
 * CompressorProcessor — Transparent utility compressor (SSL G-Bus style).
 *
 * Architecture:
 *   - Custom envelope follower with soft knee for accurate gain computation.
 *   - juce::dsp::Compressor is used as a gain computer reference; actual
 *     processing uses our custom soft-knee envelope follower.
 *   - Gain reduction meter: std::atomic<float>, written by processBlock(),
 *     read safely from any thread (UI meter, DAW automation).
 *
 * Thread safety:
 *   - All setXxx() methods write to std::atomic members or values read at
 *     block start — safe to call from message thread during audio.
 *   - processBlock() is allocation-free.
 *
 * FX-02 requirements: attack, release, threshold, ratio, knee, GR meter.
 */
class CompressorProcessor : public juce::AudioProcessor
{
public:
    CompressorProcessor();
    ~CompressorProcessor() override = default;

    void setThreshold(float dBFS);        // e.g., -20.0f
    void setRatio(float ratio);           // e.g., 4.0f (4:1)
    void setAttack(float ms);             // e.g., 10.0f ms
    void setRelease(float ms);            // e.g., 100.0f ms
    void setKnee(float dB);              // Soft knee width in dB (0 = hard knee)
    void setMakeupGain(float dB);
    void setBypass(bool bypass);

    /** Thread-safe gain reduction readout (dB, positive = attenuation). */
    float getGainReductionDB() const noexcept { return currentGainReductionDB.load(std::memory_order_relaxed); }

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "CompressorProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    // Parameters (atomic for lock-free message-thread writes)
    std::atomic<float> thresholdDB    { -20.0f };
    std::atomic<float> ratio          { 4.0f };
    std::atomic<float> attackMs       { 10.0f };
    std::atomic<float> releaseMs      { 100.0f };
    std::atomic<float> kneeDB         { 6.0f };
    std::atomic<float> makeupGainDB   { 0.0f };
    std::atomic<bool>  bypassed       { false };

    // Gain reduction meter (written by audio thread, read by message thread)
    mutable std::atomic<float> currentGainReductionDB { 0.0f };

    // Envelope follower state (audio thread only)
    double sampleRate = 44100.0;
    float envelopeLinear = 0.0f;  // Current envelope level (linear)
    float gainReductionLinear = 1.0f;  // Current gain reduction factor

    // Compute soft-knee gain reduction (dB) for a given input level (dBFS)
    float computeGainReductionDB(float inputDB,
                                  float thresh, float rat, float knee) const noexcept;

    // Convert attack/release ms to per-sample coefficient
    static float msToCoeff(float ms, double sr) noexcept;
};
