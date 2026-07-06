#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>

class BuiltInPluginFormat;

/**
 * DelayProcessor — Stereo delay with tempo sync, feedback filtering and ping-pong.
 *
 * Architecture:
 *   - Circular delay line per channel, allocated in prepareToPlay() (max 4 s).
 *   - Delay time is smoothed per sample (one-pole) and read with linear
 *     interpolation — time changes glide instead of clicking.
 *   - Feedback path runs through a one-pole high-pass + low-pass so repeats
 *     get darker/thinner like an analog delay.
 *   - Tempo sync reads the host playhead BPM (fallback 120) and derives the
 *     delay time from the selected note division.
 *
 * Thread safety: parameters are std::atomic mirrors of APVTS values;
 * processBlock() is allocation-free (buffers pre-sized in prepareToPlay).
 */
class DelayProcessor : public juce::AudioPluginInstance,
                       public juce::AudioProcessorValueTreeState::Listener
{
public:
    DelayProcessor();
    ~DelayProcessor() override = default;

    void setTimeMs(float ms);
    void setSyncIndex(int index);          // 0 = free (ms), 1.. = note divisions
    void setFeedback(float percent);       // 0..95
    void setMix(float percent);            // 0..100
    void setPingPong(bool enabled);

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    const juce::String getName() const override { return "DelayProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
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

    /** Note divisions offered by the "sync" parameter (index 0 = free running). */
    static juce::StringArray syncChoices();
    /** Beats per repeat for a sync index (>0); 0 for free running. */
    static double beatsForSyncIndex (int index) noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float> timeMs      { 350.0f };
    std::atomic<int>   syncIndex   { 0 };
    std::atomic<float> feedbackPct { 35.0f };
    std::atomic<float> hpfHz       { 90.0f };
    std::atomic<float> lpfHz       { 12000.0f };
    std::atomic<float> mixPct      { 25.0f };
    std::atomic<bool>  pingPong    { false };

    // Audio-thread state
    double sampleRate = 44100.0;
    std::vector<float> delayLine[2];       // circular buffers (sized in prepareToPlay)
    int   writePos = 0;
    int   lineLength = 0;
    float smoothedDelaySamples = 0.0f;
    float fbLpState[2] { 0.0f, 0.0f };
    float fbHpState[2] { 0.0f, 0.0f };

    float currentDelaySamplesTarget() noexcept;   // resolves sync/BPM → samples

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayProcessor)
};
