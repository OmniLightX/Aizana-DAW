#include "EQProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include "ui/eq/EQComponent.h"

// ---------------------------------------------------------------------------
// EQEditor — thin AudioProcessorEditor wrapper for EQComponent
// ---------------------------------------------------------------------------
class EQEditor : public juce::AudioProcessorEditor
{
public:
    EQEditor (EQProcessor& proc)
        : juce::AudioProcessorEditor (proc), eqComponent (proc)
    {
        addAndMakeVisible (eqComponent);
        setSize (700, 350);
    }

    void resized() override { eqComponent.setBounds (getLocalBounds()); }

private:
    EQComponent eqComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQEditor)
};

// ---------------------------------------------------------------------------

EQProcessor::EQProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Default: all bands are flat peak filters (0 dB gain = unity)
    for (int i = 0; i < NumBands; ++i)
    {
        bands[i] = EQBand{};
        bands[i].enabled = false;  // Start with all bands bypassed
    }
}

void EQProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maximumExpectedSamplesPerBlock);
    spec.numChannels      = 2;

    eqChain.prepare(spec);

    // Apply all current band coefficients
    for (int i = 0; i < NumBands; ++i)
        updateBandCoefficients(i);

    // Prepare spectrum FIFO buffer (no reallocation needed if already correct size)
    spectrumBuffer.setSize(1, SpectrumFifoSize, false, true, false);
}

void EQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // -----------------------------------------------------------------------
    // Audio thread — zero dynamic allocations in this function.
    // -----------------------------------------------------------------------
    juce::ScopedNoDenormals noDenormals;

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    eqChain.process(ctx);

    // Push samples into spectrum FIFO (lock-free)
    // Message thread reads this in a timer callback to run the FFT.
    const int numSamples = buffer.getNumSamples();
    int start1, size1, start2, size2;
    spectrumFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 > 0)
        spectrumBuffer.copyFrom(0, start1, buffer, 0, 0, size1);
    if (size2 > 0)
        spectrumBuffer.copyFrom(0, start2, buffer, 0, size1, size2);

    spectrumFifo.finishedWrite(size1 + size2);
}

void EQProcessor::setBand(int bandIndex, const EQBand& band)
{
    jassert(bandIndex >= 0 && bandIndex < NumBands);
    bands[bandIndex] = band;
    updateBandCoefficients(bandIndex);
}

EQBand EQProcessor::getBand(int bandIndex) const
{
    jassert(bandIndex >= 0 && bandIndex < NumBands);
    return bands[bandIndex];
}

bool EQProcessor::getSpectrumData(std::vector<float>& magnitudesOut, int fftSize)
{
    // Message-thread only. Reads from spectrum FIFO.
    int available = spectrumFifo.getNumReady();
    if (available < fftSize)
        return false;

    int start1, size1, start2, size2;
    spectrumFifo.prepareToRead(fftSize, start1, size1, start2, size2);

    // Copy to user buffer (this is the message thread — allocation is fine here)
    magnitudesOut.resize(static_cast<size_t>(fftSize));
    const float* fifoData = spectrumBuffer.getReadPointer(0);
    std::copy(fifoData + start1, fifoData + start1 + size1, magnitudesOut.begin());
    if (size2 > 0)
        std::copy(fifoData + start2, fifoData + start2 + size2,
                  magnitudesOut.begin() + size1);

    spectrumFifo.finishedRead(size1 + size2);
    return true;
}

void EQProcessor::updateBandCoefficients(int bandIndex)
{
    if (currentSampleRate <= 0.0)
        return;

    const EQBand& b = bands[bandIndex];

    juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

    if (!b.enabled || std::abs(b.gainDB) < 1e-4f)
    {
        // Pass-through: unity gain allpass-equivalent (bypass via gain=0dB peak)
        coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate,
            juce::jlimit(20.0f, (float)(currentSampleRate * 0.499), b.frequencyHz),
            juce::jmax(0.1f, b.Q),
            1.0f);  // gainFactor = 1.0 = 0dB
    }
    else
    {
        float freq = juce::jlimit(20.0f, (float)(currentSampleRate * 0.499), b.frequencyHz);
        float Q    = juce::jmax(0.1f, b.Q);
        float gain = std::pow(10.0f, b.gainDB / 20.0f);  // Convert dB to linear factor

        switch (b.type)
        {
            case EQBandType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                    currentSampleRate, freq, Q, gain);
                break;

            case EQBandType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                    currentSampleRate, freq, Q, gain);
                break;

            case EQBandType::LowPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
                    currentSampleRate, freq, Q);
                break;

            case EQBandType::HighPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
                    currentSampleRate, freq, Q);
                break;

            case EQBandType::Peak:
            default:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                    currentSampleRate, freq, Q, gain);
                break;
        }
    }

    applyCoefficients(bandIndex, coeffs);
}

void EQProcessor::applyCoefficients(int bandIndex,
    juce::dsp::IIR::Coefficients<float>::Ptr coeffs)
{
    // JUCE IIR filter coefficients use ReferenceCountedObjectPtr.
    // Assigning to the filter's coefficients member is safe to call from any thread
    // because JUCE uses atomic ref-counting for the shared pointer handoff.
    switch (bandIndex)
    {
        case 0: *eqChain.get<0>().coefficients = *coeffs; break;
        case 1: *eqChain.get<1>().coefficients = *coeffs; break;
        case 2: *eqChain.get<2>().coefficients = *coeffs; break;
        case 3: *eqChain.get<3>().coefficients = *coeffs; break;
        default: jassertfalse; break;
    }
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void EQProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Cloudes EQ");
}

juce::AudioProcessorEditor* EQProcessor::createEditor()
{
    return new EQEditor (*this);
}
