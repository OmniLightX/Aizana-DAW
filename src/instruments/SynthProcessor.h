#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/**
 * SynthProcessor — "Aizana Synth": a self-contained subtractive polysynth for
 * keys / bass / leads. Two detuned oscillators (saw/square/triangle/sine) →
 * resonant TPT state-variable low-pass with its own envelope → ADSR amp.
 * Fully procedural: no samples, ships in the binary, always plays.
 *
 * Built on juce::Synthesiser (16-voice polyphony, sample-accurate MIDI).
 * processBlock is allocation-/lock-free (I-01). Second P0 instrument next to
 * the drum machine so a fresh user can play melodic parts out of the box.
 */
class SynthProcessor : public juce::AudioPluginInstance
{
public:
    SynthProcessor();
    ~SynthProcessor() override = default;

    // Live params, mirrored from the APVTS each block and read by the voices.
    struct Params
    {
        std::atomic<int>   wave        { 0 };      // 0 Saw · 1 Square · 2 Triangle · 3 Sine
        std::atomic<float> cutoff      { 3000.0f };
        std::atomic<float> resonance   { 0.2f };
        std::atomic<float> attack      { 0.005f };
        std::atomic<float> decay       { 0.20f };
        std::atomic<float> sustain     { 0.75f };
        std::atomic<float> release     { 0.25f };
        std::atomic<float> filterEnv   { 0.4f };   // 0..1 cutoff modulation depth
        std::atomic<float> volume      { 0.8f };
    };

    // -------------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}

    const juce::String getName() const override { return "Aizana Synth"; }
    bool   acceptsMidi()  const override { return true;  }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    // Factory presets exposed as AudioProcessor programs (Bass/Lead/Pad/…).
    int  getNumPrograms() override;
    int  getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorEditor* createEditor() override;   // preset combo + knobs
    bool hasEditor() const override { return true; }

    void fillInPluginDescription (juce::PluginDescription&) const override;

    static juce::StringArray waveChoices() { return { "Saw", "Square", "Triangle", "Sine" }; }
    static juce::StringArray presetNames();

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::Synthesiser synth;
    Params params;
    int currentProgram = 0;

    void syncParams();
    void applyProgram (int index);   // writes a preset's values into the APVTS

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthProcessor)
};
