#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/**
 * DeEsserProcessor — slim split-band de-esser, FabFilter Pro-DS inspired.
 *
 * Tames vocal sibilance ("s"/"sh"/"t" hiss) without dulling the whole track.
 *
 * Architecture (the Pro-DS essence, minus the proprietary detection):
 *   - A Linkwitz-Riley crossover splits the signal into a low band and a high
 *     (sibilance) band at `frequency`. The dual-output processSample() keeps the
 *     two bands phase-coherent so they sum back flat.
 *   - The high band IS the side-chain: a fast peak follower tracks its level.
 *     When it exceeds `threshold`, the band is attenuated by the overshoot,
 *     limited to `range` dB of maximum reduction (Pro-DS "Range").
 *   - Split mode: only the high band is ducked (natural for vocals).
 *     Wide mode:  the whole signal is ducked (single-band behaviour).
 *   - Two meters (atomic, audio→UI): gain reduction + side-chain level.
 *
 * Controls mirror Pro-DS's core: Threshold, Range, Frequency, Mode. Ballistics
 * are fixed-fast (de-esser standard) to stay slim.
 *
 * Thread safety: setXxx() write std::atomics; processBlock() is alloc/lock-free.
 */
class DeEsserProcessor : public juce::AudioPluginInstance,
                         public juce::AudioProcessorValueTreeState::Listener
{
public:
    enum class Mode { Split = 0, Wide = 1 };

    DeEsserProcessor();
    ~DeEsserProcessor() override = default;

    void setThreshold (float dBFS);   // side-chain trigger level, e.g. -24
    void setRange (float dB);         // max gain reduction, e.g. 10
    void setFrequency (float hz);     // sibilance crossover, e.g. 6500
    void setMode (Mode);
    void setBypass (bool);

    /** Thread-safe gain reduction readout (dB, positive = attenuation). */
    float getGainReductionDB() const noexcept { return currentGainReductionDB.load (std::memory_order_relaxed); }
    /** Thread-safe side-chain (high-band) level readout (dBFS) for the meter. */
    float getSidechainLevelDB() const noexcept { return currentScLevelDB.load (std::memory_order_relaxed); }

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "DeEsserProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // AudioPluginInstance interface
    void fillInPluginDescription (juce::PluginDescription& desc) const override;

    // AudioProcessorValueTreeState::Listener
    void parameterChanged (const juce::String& parameterID, float newValue) override;

private:
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> thresholdDB { -24.0f };
    std::atomic<float> rangeDB     {  10.0f };
    std::atomic<float> freqHz      { 6500.0f };
    std::atomic<int>   mode        { 0 };       // 0 = Split, 1 = Wide
    std::atomic<bool>  bypassed    { false };

    // Meters (audio-thread write, message-thread read)
    mutable std::atomic<float> currentGainReductionDB { 0.0f };
    mutable std::atomic<float> currentScLevelDB       { -120.0f };

    // Audio-thread state
    double sampleRate = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> crossover;   // dual-output band split
    float envelopeLinear = 0.0f;

    static float msToCoeff (float ms, double sr) noexcept;
};
