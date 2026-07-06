#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

class BuiltInPluginFormat;

/**
 * SaturatorProcessor — Analog-style saturation (Tape / Tube / Soft Clip).
 *
 * Architecture:
 *   - Waveshaper per sample: Tape = symmetric tanh; Tube = biased tanh
 *     (even harmonics) with a DC blocker after the asymmetry; Soft = cubic
 *     soft clipper.
 *   - Auto-gain keeps small-signal level at unity (divide by drive), so
 *     turning drive up saturates peaks instead of just getting louder.
 *   - Tone: post-shaper tilt (one-pole crossover at 800 Hz, ±6 dB tilt).
 *   - No oversampling in v1 — acceptable aliasing at musical drive settings;
 *     revisit with juce::dsp::Oversampling if it becomes audible.
 *
 * Thread safety: parameters are std::atomic mirrors of APVTS values;
 * processBlock() is allocation-free.
 */
class SaturatorProcessor : public juce::AudioPluginInstance,
                           public juce::AudioProcessorValueTreeState::Listener
{
public:
    SaturatorProcessor();
    ~SaturatorProcessor() override = default;

    void setDrive(float dB);         // 0..36
    void setTypeIndex(int index);    // 0 = Tape, 1 = Tube, 2 = Soft Clip
    void setTone(float percent);     // -100..100 (dark..bright)
    void setMix(float percent);      // 0..100
    void setOutputGain(float dB);    // -12..12

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "SaturatorProcessor"; }
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

    std::atomic<float> driveDB     { 12.0f };
    std::atomic<int>   typeIndex   { 0 };
    std::atomic<float> tonePct     { 0.0f };
    std::atomic<float> mixPct      { 100.0f };
    std::atomic<float> outputDB    { 0.0f };

    // Audio-thread state
    double sampleRate = 44100.0;
    float  tiltLpState[2] { 0.0f, 0.0f };   // 800 Hz crossover for the tilt
    float  dcState[2]     { 0.0f, 0.0f };   // DC blocker (Tube asymmetry)
    float  dcPrevIn[2]    { 0.0f, 0.0f };

    static float shape(float x, int type) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaturatorProcessor)
};
