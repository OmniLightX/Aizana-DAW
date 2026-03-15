#include "LimiterProcessor.h"
#include <cmath>

LimiterProcessor::LimiterProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void LimiterProcessor::setThreshold(float dBFS)
{
    thresholdDB.store(dBFS, std::memory_order_relaxed);
    paramsDirty.store(true, std::memory_order_relaxed);
}

void LimiterProcessor::setRelease(float ms)
{
    releaseMs.store(ms, std::memory_order_relaxed);
    paramsDirty.store(true, std::memory_order_relaxed);
}

void LimiterProcessor::prepareToPlay(double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels      = 2;

    reconfigureLimiter();
    limiter.prepare(spec);
    paramsDirty.store(false, std::memory_order_relaxed);
    gainReductionDB.store(0.0f, std::memory_order_relaxed);
}

void LimiterProcessor::reconfigureLimiter()
{
    float thresh = thresholdDB.load(std::memory_order_relaxed);
    float rel    = releaseMs.load(std::memory_order_relaxed);

    limiter.setThreshold(thresh);
    limiter.setRelease(rel);
}

void LimiterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // -----------------------------------------------------------------------
    // Audio thread — zero dynamic allocations.
    // -----------------------------------------------------------------------
    juce::ScopedNoDenormals noDenormals;

    // Reconfigure if parameters changed (atomic reads — no lock)
    if (paramsDirty.load(std::memory_order_relaxed))
    {
        reconfigureLimiter();
        paramsDirty.store(false, std::memory_order_relaxed);
    }

    // Measure peak input level before limiting (for GR computation)
    float peakIn = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            peakIn = std::max(peakIn, std::abs(data[n]));
    }
    float peakInDB = (peakIn > 1e-10f) ? 20.0f * std::log10(peakIn) : -120.0f;

    // Process through JUCE limiter
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    limiter.process(ctx);

    // Measure peak output level for GR meter
    float peakOut = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            peakOut = std::max(peakOut, std::abs(data[n]));
    }
    float peakOutDB = (peakOut > 1e-10f) ? 20.0f * std::log10(peakOut) : -120.0f;

    // GR = input peak - output peak (positive when limiting is occurring)
    float gr = juce::jmax(0.0f, peakInDB - peakOutDB);
    gainReductionDB.store(gr, std::memory_order_relaxed);
}
