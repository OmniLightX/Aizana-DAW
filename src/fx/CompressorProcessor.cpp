#include "CompressorProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

CompressorProcessor::CompressorProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CompressorProcessor::setThreshold(float dBFS)  { thresholdDB.store(dBFS, std::memory_order_relaxed); }
void CompressorProcessor::setRatio(float r)          { ratio.store(r, std::memory_order_relaxed); }
void CompressorProcessor::setAttack(float ms)        { attackMs.store(ms, std::memory_order_relaxed); }
void CompressorProcessor::setRelease(float ms)       { releaseMs.store(ms, std::memory_order_relaxed); }
void CompressorProcessor::setKnee(float dB)          { kneeDB.store(dB, std::memory_order_relaxed); }
void CompressorProcessor::setMakeupGain(float dB)    { makeupGainDB.store(dB, std::memory_order_relaxed); }
void CompressorProcessor::setBypass(bool bypass)     { bypassed.store(bypass, std::memory_order_relaxed); }

void CompressorProcessor::prepareToPlay(double sr, int)
{
    sampleRate = sr;
    envelopeLinear = 0.0f;
    gainReductionLinear = 1.0f;
    currentGainReductionDB.store(0.0f, std::memory_order_relaxed);
}

float CompressorProcessor::msToCoeff(float ms, double sr) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    // Time constant: coefficient = e^(-1 / (sr * t))
    return std::exp(-1.0f / (static_cast<float>(sr) * ms * 0.001f));
}

float CompressorProcessor::computeGainReductionDB(float inputDB,
                                                   float thresh,
                                                   float rat,
                                                   float knee) const noexcept
{
    // SSL G-Bus soft-knee transfer function:
    //   - Below (threshold - knee/2): no compression
    //   - Within knee: quadratic interpolation
    //   - Above (threshold + knee/2): full ratio compression
    float halfKnee = knee * 0.5f;
    float lowerKnee = thresh - halfKnee;
    float upperKnee = thresh + halfKnee;

    if (inputDB <= lowerKnee)
        return 0.0f;  // Below knee: no gain reduction

    if (inputDB >= upperKnee || knee <= 0.0f)
    {
        // Above knee (or hard knee): full ratio
        float overshoot = inputDB - thresh;
        float targetDB  = thresh + overshoot / rat;
        return inputDB - targetDB;  // Gain reduction amount (positive = attenuation)
    }

    // Inside soft knee: quadratic interpolation
    // GR = (1 - 1/ratio) * (inputDB - lowerKnee)^2 / (2 * knee)
    float delta = inputDB - lowerKnee;
    float gr = (1.0f - 1.0f / rat) * (delta * delta) / (2.0f * knee);
    return gr;
}

void CompressorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Load parameters (atomic reads — safe from audio thread)
    bool bypass      = bypassed.load(std::memory_order_relaxed);
    float thresh     = thresholdDB.load(std::memory_order_relaxed);
    float rat        = ratio.load(std::memory_order_relaxed);
    float attMs      = attackMs.load(std::memory_order_relaxed);
    float relMs      = releaseMs.load(std::memory_order_relaxed);
    float knee       = kneeDB.load(std::memory_order_relaxed);
    float makeupDB   = makeupGainDB.load(std::memory_order_relaxed);

    if (bypass)
        return;  // Pass-through: no processing needed

    float attackCoeff  = msToCoeff(attMs, sampleRate);
    float releaseCoeff = msToCoeff(relMs, sampleRate);
    float makeupGain   = std::pow(10.0f, makeupDB / 20.0f);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float maxGainReductionDB = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        // Detect: peak across all channels
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max(peak, std::abs(buffer.getSample(ch, n)));

        // Level detection: peak envelope follower
        float env = envelopeLinear;
        if (peak > env)
            env = attackCoeff * env + (1.0f - attackCoeff) * peak;    // Attack
        else
            env = releaseCoeff * env + (1.0f - releaseCoeff) * peak;  // Release
        envelopeLinear = env;

        // Gain computation: convert envelope to dB, compute GR
        float inputDB = (env > 1e-10f) ? (20.0f * std::log10(env)) : -120.0f;
        float grDB    = computeGainReductionDB(inputDB, thresh, rat, knee);
        float grLinear = std::pow(10.0f, -grDB / 20.0f);  // Invert: GR reduces gain

        // Apply gain reduction + makeup gain
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float s = buffer.getSample(ch, n);
            buffer.setSample(ch, n, s * grLinear * makeupGain);
        }

        // Track maximum GR this block for the meter
        if (grDB > maxGainReductionDB)
            maxGainReductionDB = grDB;
    }

    // Update GR meter (audio thread write, message thread read)
    currentGainReductionDB.store(maxGainReductionDB, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void CompressorProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Cloudes Compressor");
}

juce::AudioProcessorEditor* CompressorProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
