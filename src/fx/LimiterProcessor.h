#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/**
 * LimiterProcessor — Hard brick-wall limiter for master bus.
 *
 * Uses juce::dsp::Limiter<float> — JUCE's lookahead limiter implementation.
 * Provides 0 dBFS hard ceiling with configurable threshold and release.
 *
 * Thread-safe gain reduction meter:
 *   - std::atomic<float> gainReductionDB is written by processBlock(),
 *     readable from any thread (UI meter display, automation).
 *
 * Audio-thread discipline:
 *   - processBlock() is allocation-free.
 *   - juce::dsp::Limiter is prepared in prepareToPlay() (allocation OK there).
 *
 * FX-04 requirements: hard limiter, 0 dBFS default ceiling, GR meter.
 */
class LimiterProcessor : public juce::AudioProcessor
{
public:
    LimiterProcessor();
    ~LimiterProcessor() override = default;

    void setThreshold(float dBFS);  // Default: 0.0 dBFS
    void setRelease(float ms);      // Default: 100ms

    /** Thread-safe gain reduction readout (dB, positive = attenuation). */
    float getGainReductionDB() const noexcept
    {
        return gainReductionDB.load(std::memory_order_relaxed);
    }

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "LimiterProcessor"; }
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
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    juce::dsp::Limiter<float> limiter;

    std::atomic<float> thresholdDB { 0.0f };
    std::atomic<float> releaseMs   { 100.0f };

    // Written by audio thread, read by message thread
    mutable std::atomic<float> gainReductionDB { 0.0f };

    double currentSampleRate = 44100.0;

    // True when parameters have changed and the limiter needs reconfiguring
    std::atomic<bool> paramsDirty { true };

    void reconfigureLimiter();
};
