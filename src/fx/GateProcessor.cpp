#include "GateProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

namespace
{
    constexpr float kHysteresisDB = 3.0f;   // close 3 dB below the open threshold
}

GateProcessor::GateProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
          .withOutput("Output",    juce::AudioChannelSet::stereo(), true)
          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "GateState",
        {
            std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold",     -80.0f, 0.0f,    -40.0f),
            std::make_unique<juce::AudioParameterFloat>("attack",    "Attack (ms)",    0.01f, 100.0f,    0.5f),
            std::make_unique<juce::AudioParameterFloat>("hold",      "Hold (ms)",      0.0f, 500.0f,    50.0f),
            std::make_unique<juce::AudioParameterFloat>("release",   "Release (ms)",   5.0f, 2000.0f,  100.0f),
            std::make_unique<juce::AudioParameterFloat>("range",     "Range (dB)",     0.0f, 80.0f,     80.0f),
            std::make_unique<juce::AudioParameterBool> ("sckey",     "Sidechain Key",  false),
        })
{
    for (auto* id : { "threshold", "attack", "hold", "release", "range", "sckey" })
        apvts.addParameterListener (id, this);
}

void GateProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if      (paramID == "threshold") thresholdDB.store(newValue, std::memory_order_relaxed);
    else if (paramID == "attack")    attackMs.store(newValue,    std::memory_order_relaxed);
    else if (paramID == "hold")      holdMs.store(newValue,      std::memory_order_relaxed);
    else if (paramID == "release")   releaseMs.store(newValue,   std::memory_order_relaxed);
    else if (paramID == "range")     rangeDB.store(newValue,     std::memory_order_relaxed);
    else if (paramID == "sckey")     scKeyEnabled.store(newValue >= 0.5f, std::memory_order_relaxed);
}

void GateProcessor::setThreshold(float dBFS) { thresholdDB.store(dBFS, std::memory_order_relaxed); }
void GateProcessor::setAttack(float ms)      { attackMs.store(ms, std::memory_order_relaxed); }
void GateProcessor::setHold(float ms)        { holdMs.store(ms, std::memory_order_relaxed); }
void GateProcessor::setRelease(float ms)     { releaseMs.store(ms, std::memory_order_relaxed); }
void GateProcessor::setRange(float dB)       { rangeDB.store(dB, std::memory_order_relaxed); }

void GateProcessor::setSidechainKeyEnabled(bool useKey)
{
    if (auto* p = apvts.getParameter("sckey"))
        p->setValueNotifyingHost(useKey ? 1.0f : 0.0f);
    else
        scKeyEnabled.store(useKey, std::memory_order_relaxed);
}

bool GateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainIn != juce::AudioChannelSet::stereo() && mainIn != juce::AudioChannelSet::mono())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void GateProcessor::prepareToPlay (double sr, int)
{
    sampleRate  = sr;
    envelope    = 0.0f;
    gateGain    = 1.0f;
    holdCounter = 0;
    gateOpen    = false;
    currentGainReductionDB.store(0.0f, std::memory_order_relaxed);
}

float GateProcessor::msToCoeff (float ms, double sr) noexcept
{
    if (ms <= 0.0f) return 0.0f;
    return std::exp(-1.0f / (static_cast<float>(sr) * ms * 0.001f));
}

void GateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float thresh  = thresholdDB.load(std::memory_order_relaxed);
    const float attMs   = attackMs.load(std::memory_order_relaxed);
    const float holdLen = holdMs.load(std::memory_order_relaxed);
    const float relMs   = releaseMs.load(std::memory_order_relaxed);
    const float range   = rangeDB.load(std::memory_order_relaxed);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Main bus first, key channels behind it (see CompressorProcessor)
    const int mainChannels = juce::jmin (numChannels,
                                         juce::jmax (1, getMainBusNumInputChannels()));
    const int scChannels   = (getBusCount (true) > 1) ? getChannelCountOfBus (true, 1) : 0;
    const bool haveKey     = scKeyEnabled.load(std::memory_order_relaxed)
                          && scChannels > 0
                          && numChannels >= mainChannels + scChannels;

    const int detectStart = haveKey ? mainChannels : 0;
    const int detectEnd   = haveKey ? mainChannels + scChannels : mainChannels;

    const float openThreshLin  = std::pow (10.0f, thresh / 20.0f);
    const float closeThreshLin = std::pow (10.0f, (thresh - kHysteresisDB) / 20.0f);
    const float rangeGain      = std::pow (10.0f, -range / 20.0f);

    const float attackCoeff  = msToCoeff (attMs, sampleRate);
    const float releaseCoeff = msToCoeff (relMs, sampleRate);
    const int   holdSamples  = (int) (holdLen * 0.001f * (float) sampleRate);

    // Fast detector decay (~2 ms) — the gate timing comes from attack/release ramps
    const float detectorDecay = msToCoeff (2.0f, sampleRate);

    float maxGainReductionDB = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float peak = 0.0f;
        for (int ch = detectStart; ch < detectEnd; ++ch)
            peak = std::max (peak, std::abs (buffer.getSample (ch, n)));

        envelope = std::max (peak, envelope * detectorDecay);

        // Hysteresis keeps the gate from chattering at the threshold
        if (envelope >= openThreshLin)
        {
            gateOpen    = true;
            holdCounter = holdSamples;
        }
        else if (gateOpen && envelope < closeThreshLin)
        {
            if (holdCounter > 0)
                --holdCounter;
            else
                gateOpen = false;
        }

        const float targetGain = gateOpen ? 1.0f : rangeGain;
        const float coeff      = (targetGain > gateGain) ? attackCoeff : releaseCoeff;
        gateGain = coeff * gateGain + (1.0f - coeff) * targetGain;

        for (int ch = 0; ch < mainChannels; ++ch)
            buffer.setSample (ch, n, buffer.getSample (ch, n) * gateGain);

        const float grDB = (gateGain > 1e-6f) ? -20.0f * std::log10 (gateGain) : range;
        if (grDB > maxGainReductionDB)
            maxGainReductionDB = grDB;
    }

    currentGainReductionDB.store(maxGainReductionDB, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void GateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void GateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void GateProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Gate");
}

juce::AudioProcessorEditor* GateProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
