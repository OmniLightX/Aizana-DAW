#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MeterProcessor;

/**
 * MasterMeterAudioProcessor — thin AudioProcessor tap for the Tracktion Engine
 * global output pipeline.
 *
 * Registered with te::DeviceManager::setGlobalOutputAudioProcessor(). TE inserts
 * this processor at the final stage of its audio render pipeline — AFTER all tracks
 * are mixed — so processBlock() receives the true mixed output, not just the
 * metronome-only secondary temp buffer captured by the secondary AudioIODeviceCallback.
 *
 * Design constraints:
 *   - processBlock() runs on the audio thread: ONLY atomics and FIFO writes — no locks,
 *     no allocations, no I/O.
 *   - Audio is passed through UNMODIFIED (read-only tap). This processor is a meter
 *     probe, not an effect.
 *   - prepareToPlay() / releaseResources() delegate to MeterProcessor so the FIFO
 *     and libebur128 state are configured for the real device parameters.
 *
 * Thread safety:
 *   - processBlock(): audio thread only. Calls MeterProcessor::pushAudioData() which
 *     is designed for audio-thread use (atomics + lock-free FIFO).
 *   - prepareToPlay() / releaseResources(): message thread (called by TE device manager).
 */
class MasterMeterAudioProcessor : public juce::AudioProcessor
{
public:
    /** Constructs the tap. Does NOT take ownership of meter — AudioEngine owns it. */
    explicit MasterMeterAudioProcessor (MeterProcessor& meter);
    ~MasterMeterAudioProcessor() override = default;

    // -----------------------------------------------------------------------
    // AudioProcessor interface — lifecycle
    // -----------------------------------------------------------------------

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    // -----------------------------------------------------------------------
    // AudioProcessor interface — processing (audio thread)
    // -----------------------------------------------------------------------

    /** Taps the buffer for metering, then passes it through unmodified. */
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    // -----------------------------------------------------------------------
    // AudioProcessor required stubs — this is a meter probe, not an effect
    // -----------------------------------------------------------------------

    const juce::String getName() const override { return "MasterMeter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    MeterProcessor& masterMeterProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterMeterAudioProcessor)
};
