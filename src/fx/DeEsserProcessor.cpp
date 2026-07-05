#include "DeEsserProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

DeEsserProcessor::DeEsserProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "DeEsserState",
        {
            std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold",
                juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -24.0f),
            std::make_unique<juce::AudioParameterFloat>("range", "Range (dB)",
                juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 10.0f),
            std::make_unique<juce::AudioParameterFloat>("frequency", "Frequency (Hz)",
                juce::NormalisableRange<float>(2000.0f, 16000.0f, 1.0f, 0.4f), 6500.0f),
            std::make_unique<juce::AudioParameterChoice>("mode", "Mode",
                juce::StringArray { "Split", "Wide" }, 0),
        })
{
    apvts.addParameterListener ("threshold", this);
    apvts.addParameterListener ("range",     this);
    apvts.addParameterListener ("frequency", this);
    apvts.addParameterListener ("mode",      this);
}

void DeEsserProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if      (paramID == "threshold") thresholdDB.store (newValue, std::memory_order_relaxed);
    else if (paramID == "range")     rangeDB.store     (newValue, std::memory_order_relaxed);
    else if (paramID == "frequency") freqHz.store      (newValue, std::memory_order_relaxed);
    else if (paramID == "mode")      mode.store        ((int) newValue, std::memory_order_relaxed);
}

void DeEsserProcessor::setThreshold (float dBFS) { thresholdDB.store (dBFS, std::memory_order_relaxed); }
void DeEsserProcessor::setRange     (float dB)   { rangeDB.store     (dB,   std::memory_order_relaxed); }
void DeEsserProcessor::setFrequency (float hz)   { freqHz.store      (hz,   std::memory_order_relaxed); }
void DeEsserProcessor::setMode      (Mode m)     { mode.store        ((int) m, std::memory_order_relaxed); }
void DeEsserProcessor::setBypass    (bool b)     { bypassed.store    (b,    std::memory_order_relaxed); }

float DeEsserProcessor::msToCoeff (float ms, double sr) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    return std::exp (-1.0f / (static_cast<float> (sr) * ms * 0.001f));
}

void DeEsserProcessor::prepareToPlay (double sr, int maxBlock)
{
    sampleRate = sr;
    envelopeLinear = 0.0f;
    currentGainReductionDB.store (0.0f, std::memory_order_relaxed);
    currentScLevelDB.store (-120.0f, std::memory_order_relaxed);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sr;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maxBlock);
    spec.numChannels      = 2;

    crossover.prepare (spec);
    crossover.setCutoffFrequency (freqHz.load (std::memory_order_relaxed));
    crossover.reset();
}

void DeEsserProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool  bypass = bypassed.load    (std::memory_order_relaxed);
    const float thresh = thresholdDB.load (std::memory_order_relaxed);
    const float range  = rangeDB.load     (std::memory_order_relaxed);
    const float freq   = freqHz.load      (std::memory_order_relaxed);
    const int   md     = mode.load        (std::memory_order_relaxed);

    if (bypass)
        return;

    crossover.setCutoffFrequency (freq);

    // De-esser ballistics: fast enough to catch a single sibilant, slow enough
    // to release between syllables without chattering.
    const float attackCoeff  = msToCoeff (0.5f, sampleRate);
    const float releaseCoeff = msToCoeff (40.0f, sampleRate);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    float maxGainReductionDB = 0.0f;
    float maxScLevelDB        = -120.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float low[2]  = { 0.0f, 0.0f };
        float high[2] = { 0.0f, 0.0f };
        float scPeak  = 0.0f;

        // Split each channel; the high band drives detection.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            crossover.processSample (ch, buffer.getSample (ch, n), low[ch], high[ch]);
            scPeak = std::max (scPeak, std::abs (high[ch]));
        }

        // Peak envelope on the side-chain (high band).
        float env = envelopeLinear;
        if (scPeak > env)
            env = attackCoeff  * env + (1.0f - attackCoeff)  * scPeak;
        else
            env = releaseCoeff * env + (1.0f - releaseCoeff) * scPeak;
        envelopeLinear = env;

        const float scDB = (env > 1e-10f) ? (20.0f * std::log10 (env)) : -120.0f;

        // Reduce by the overshoot above threshold, capped at Range (Pro-DS).
        const float grDB    = juce::jlimit (0.0f, range, scDB - thresh);
        const float grLinear = std::pow (10.0f, -grDB / 20.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float out = (md == (int) Mode::Wide)
                                 ? (low[ch] + high[ch]) * grLinear   // duck whole signal
                                 : low[ch] + high[ch] * grLinear;    // duck only sibilance
            buffer.setSample (ch, n, out);
        }

        maxGainReductionDB = std::max (maxGainReductionDB, grDB);
        maxScLevelDB        = std::max (maxScLevelDB, scDB);
    }

    currentGainReductionDB.store (maxGainReductionDB, std::memory_order_relaxed);
    currentScLevelDB.store (maxScLevelDB, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void DeEsserProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DeEsserProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void DeEsserProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana De-Esser");
}

juce::AudioProcessorEditor* DeEsserProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
