#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// Forward declare so processors can call BuiltInPluginFormat::getDescriptionFor
// without a circular include — the full include is in the .cpp file.
class BuiltInPluginFormat;

enum class EQBandType { LowShelf, Peak, HighShelf, LowPass, HighPass };

struct EQBand
{
    EQBandType type      = EQBandType::Peak;
    float frequencyHz    = 1000.0f;
    float gainDB         = 0.0f;
    float Q              = 0.707f;
    bool  enabled        = true;
};

/**
 * EQProcessor — 4-band parametric EQ using juce::dsp::ProcessorChain.
 *
 * Audio-thread safety:
 *   - Coefficients are computed on the calling thread in setBand() via a
 *     std::atomic exchange pattern (JUCE IIR filter coefficients are
 *     reference-counted SharedResourcePointers — assigning them is atomic
 *     in JUCE 7+/8).
 *   - processBlock() is allocation-free — no dynamic memory on audio thread.
 *
 * Spectrum data:
 *   - Audio thread writes samples into spectrumFifo (lock-free).
 *   - Message thread reads FIFO in a timer callback, runs FFT, repaints.
 *   - getSpectrumData() is a message-thread-only call.
 *
 * FX-01 requirement: minimum 4 independent parametric bands.
 */
class EQProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr int NumBands = 4;

    EQProcessor();
    ~EQProcessor() override = default;

    /** Set band parameters (may be called from any thread). */
    void setBand(int bandIndex, const EQBand& band);
    EQBand getBand(int bandIndex) const;

    /**
     * Copy spectrum magnitude data (in dB) into magnitudesOut.
     * Only call from message thread. Returns false if no new data.
     */
    bool getSpectrumData(std::vector<float>& magnitudesOut, int fftSize = 2048);

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    // Required pure-virtual overrides (minimal implementation for DSP-only use)
    const juce::String getName() const override { return "EQProcessor"; }
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

private:
    using EQChain = juce::dsp::ProcessorChain<
        juce::dsp::IIR::Filter<float>,   // Band 0
        juce::dsp::IIR::Filter<float>,   // Band 1
        juce::dsp::IIR::Filter<float>,   // Band 2
        juce::dsp::IIR::Filter<float>>;  // Band 3

    EQChain eqChain;
    std::array<EQBand, NumBands> bands;
    double currentSampleRate = 44100.0;

    // Lock-free FIFO for spectrum analyzer (RESEARCH.md Pitfall 6)
    // Audio thread pushes samples; message thread reads and runs FFT.
    static constexpr int SpectrumFifoSize = 4096;
    juce::AbstractFifo spectrumFifo { SpectrumFifoSize };
    juce::AudioBuffer<float> spectrumBuffer { 1, SpectrumFifoSize };

    /** Compute IIR coefficients and apply to the chain. Call from any thread. */
    void updateBandCoefficients(int bandIndex);

    /** Apply updated coefficients to the chain (called after setBand). */
    void applyCoefficients(int bandIndex,
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs);
};
