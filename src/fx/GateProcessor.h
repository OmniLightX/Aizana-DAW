#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

class BuiltInPluginFormat;

/**
 * GateProcessor — Noise gate / expander gate with hold and external key input.
 *
 * Architecture:
 *   - Peak envelope detector on the key signal (main input or sidechain bus).
 *   - Open/close decision with 3 dB hysteresis (opens at threshold, closes at
 *     threshold − 3 dB) so signals hovering at the threshold don't chatter.
 *   - Gain ramps: attack coefficient while opening, release while closing;
 *     the hold timer keeps the gate open after the key drops below threshold.
 *   - "range" limits the attenuation depth (80 dB = effectively closed).
 *
 * Sidechain (MIX-03): same bus layout as CompressorProcessor — a second stereo
 * input bus "Sidechain"; te::Plugin::canSidechain() sees 4 inputs and wires the
 * source track into channels 2/3. "sckey" switches the detector to that input.
 *
 * Thread safety: parameters are std::atomic mirrors of APVTS values;
 * processBlock() is allocation-free.
 */
class GateProcessor : public juce::AudioPluginInstance,
                      public juce::AudioProcessorValueTreeState::Listener
{
public:
    GateProcessor();
    ~GateProcessor() override = default;

    void setThreshold(float dBFS);
    void setAttack(float ms);
    void setHold(float ms);
    void setRelease(float ms);
    void setRange(float dB);              // attenuation depth, positive dB
    void setSidechainKeyEnabled(bool useKey);
    bool isSidechainKeyEnabled() const noexcept { return scKeyEnabled.load(std::memory_order_relaxed); }

    /** Thread-safe attenuation readout (dB, positive = gate closing/closed). */
    float getGainReductionDB() const noexcept { return currentGainReductionDB.load(std::memory_order_relaxed); }

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "GateProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // AudioPluginInstance interface
    void fillInPluginDescription (juce::PluginDescription& desc) const override;

    // AudioProcessorValueTreeState::Listener
    void parameterChanged (const juce::String& parameterID, float newValue) override;

private:
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> thresholdDB  { -40.0f };
    std::atomic<float> attackMs     { 0.5f };
    std::atomic<float> holdMs       { 50.0f };
    std::atomic<float> releaseMs    { 100.0f };
    std::atomic<float> rangeDB      { 80.0f };
    std::atomic<bool>  scKeyEnabled { false };

    mutable std::atomic<float> currentGainReductionDB { 0.0f };

    // Audio-thread state
    double sampleRate = 44100.0;
    float  envelope   = 0.0f;   // key detector (linear peak)
    float  gateGain   = 1.0f;   // current gain 1..rangeGain
    int    holdCounter = 0;     // samples of hold left before release starts
    bool   gateOpen   = false;

    static float msToCoeff(float ms, double sr) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateProcessor)
};
