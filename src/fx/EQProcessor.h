#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// Forward declare so processors can call BuiltInPluginFormat::getDescriptionFor
// without a circular include — the full include is in the .cpp file.
class BuiltInPluginFormat;

enum class EQBandType { LowShelf, Peak, HighShelf, LowPass, HighPass, Notch };

struct EQBand
{
    EQBandType type      = EQBandType::Peak;
    float frequencyHz    = 1000.0f;
    float gainDB         = 0.0f;
    float Q              = 0.707f;
    bool  enabled        = true;
    int   slopeOrder     = 2;  // dB/oct slope for HP/LP: 2=12dB, 4=24dB/oct
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
    static constexpr int NumBands = 8;

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
    // ProcessorDuplicator wraps the mono IIR::Filter for stereo (and beyond).
    // IIR::Filter<float> asserts numChannels==1 — using it directly in the chain
    // with a stereo AudioBlock silently skips channel 1 (no audio change on right).
    using EQFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    using EQChain = juce::dsp::ProcessorChain<
        EQFilter,   // Band 0
        EQFilter,   // Band 1
        EQFilter,   // Band 2
        EQFilter,   // Band 3
        EQFilter,   // Band 4
        EQFilter,   // Band 5
        EQFilter,   // Band 6
        EQFilter>;  // Band 7

    EQChain eqChain;
    std::array<EQBand, NumBands> bands;
    double currentSampleRate = 44100.0;

    // Secondary IIR filters for HP/LP slope 24dB/oct (slopeOrder=4).
    // Each cascadeFilter[i] mirrors the coefficients of eqChain band i.
    // cascadeActive[i] is true only when band i is HP/LP with slopeOrder >= 4.
    std::array<EQFilter, NumBands> cascadeFilters;
    std::array<bool, NumBands> cascadeActive {};

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
