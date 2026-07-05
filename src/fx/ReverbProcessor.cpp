#include "ReverbProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Mode-specific delay configurations (in milliseconds)
// Based on Schroeder/Moorer hybrid as per RESEARCH.md Open Question 3.
// ---------------------------------------------------------------------------
struct ModeConfig
{
    int numCombs;
    int numAllpasses;
    float combDelaysMs[8];
    float allpassDelaysMs[4];
    float feedbackBase;        // Base feedback (room size scales this)
    float dampingBase;         // Base damping
};

static const ModeConfig kRoomConfig {
    4, 2,
    { 25.31f, 26.94f, 28.96f, 30.75f, 0, 0, 0, 0 },
    { 5.0f, 1.68f, 0, 0 },
    0.82f, 0.5f
};

static const ModeConfig kHallConfig {
    8, 4,
    { 36.25f, 40.04f, 43.94f, 47.41f, 51.29f, 54.92f, 58.67f, 62.44f },
    { 12.61f, 10.0f, 7.73f, 5.13f },
    0.88f, 0.25f
};

static const ModeConfig kPlateConfig {
    4, 4,
    { 30.51f, 33.98f, 41.25f, 46.43f, 0, 0, 0, 0 },
    { 8.95f, 6.28f, 4.10f, 2.57f },
    0.78f, 0.35f
};

static const ModeConfig& configForMode(ReverbMode mode)
{
    switch (mode)
    {
        case ReverbMode::Hall:  return kHallConfig;
        case ReverbMode::Plate: return kPlateConfig;
        default:                return kRoomConfig;
    }
}

// ---------------------------------------------------------------------------

ReverbProcessor::ReverbProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "ReverbState",
        {
            std::make_unique<juce::AudioParameterFloat>("roomSize",  "Room Size",         0.0f, 1.0f,   0.5f),
            std::make_unique<juce::AudioParameterFloat>("damping",   "Damping",           0.0f, 1.0f,   0.5f),
            std::make_unique<juce::AudioParameterFloat>("preDelay",  "Pre-Delay (ms)",    0.0f, 100.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>("wet",       "Wet/Dry",           0.0f, 1.0f,   0.3f),
            std::make_unique<juce::AudioParameterChoice>("mode",     "Mode",
                juce::StringArray{"Room", "Hall", "Plate"}, 0),
        })
{
    apvts.addParameterListener("roomSize", this);
    apvts.addParameterListener("damping",  this);
    apvts.addParameterListener("preDelay", this);
    apvts.addParameterListener("wet",      this);
    apvts.addParameterListener("mode",     this);
}

void ReverbProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if      (paramID == "roomSize") roomSizeParam.store(newValue,            std::memory_order_relaxed);
    else if (paramID == "damping")  dampingParam.store(newValue,             std::memory_order_relaxed);
    else if (paramID == "preDelay") preDelayMs.store(newValue,               std::memory_order_relaxed);
    else if (paramID == "wet")      wetParam.store(newValue,                 std::memory_order_relaxed);
    else if (paramID == "mode")     currentMode.store((int)newValue,         std::memory_order_relaxed);
}

void ReverbProcessor::setMode(ReverbMode mode)    { currentMode.store(static_cast<int>(mode), std::memory_order_relaxed); }
void ReverbProcessor::setRoomSize(float size)     { roomSizeParam.store(size, std::memory_order_relaxed); }
void ReverbProcessor::setDamping(float damping)   { dampingParam.store(damping, std::memory_order_relaxed); }
void ReverbProcessor::setPreDelay(float ms)       { preDelayMs.store(ms, std::memory_order_relaxed); }
void ReverbProcessor::setWetDry(float wet)        { wetParam.store(wet, std::memory_order_relaxed); }

void ReverbProcessor::prepareToPlay(double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate;

    int maxDelay = maxDelaySamplesForRate(sampleRate);

    // Pre-allocate all comb and allpass delay lines for worst-case delay
    for (auto& c : combsL)  c.prepare(maxDelay);
    for (auto& c : combsR)  c.prepare(maxDelay);
    for (auto& a : allpassL) a.prepare(maxDelay / 4);
    for (auto& a : allpassR) a.prepare(maxDelay / 4);

    // Pre-delay buffer (max 100ms)
    int maxPreDelay = static_cast<int>(sampleRate * 0.1) + 1;
    preDelayBuffer.assign(static_cast<size_t>(maxPreDelay), 0.0f);
    preDelayWrite = 0;

    // Apply current mode parameters
    applyModeParameters();
}

void ReverbProcessor::releaseResources()
{
    for (auto& c : combsL)  c.reset();
    for (auto& c : combsR)  c.reset();
    for (auto& a : allpassL) a.reset();
    for (auto& a : allpassR) a.reset();
    std::fill(preDelayBuffer.begin(), preDelayBuffer.end(), 0.0f);
}

void ReverbProcessor::applyModeParameters()
{
    ReverbMode mode = static_cast<ReverbMode>(currentMode.load(std::memory_order_relaxed));
    float roomSize  = roomSizeParam.load(std::memory_order_relaxed);
    float damping   = dampingParam.load(std::memory_order_relaxed);

    const ModeConfig& cfg = configForMode(mode);
    numActiveCombs    = cfg.numCombs;
    numActiveAllpasses = cfg.numAllpasses;

    float feedback = cfg.feedbackBase * (0.5f + 0.5f * roomSize);  // Scale by room size
    float damp     = cfg.dampingBase  + damping * (1.0f - cfg.dampingBase);

    for (int i = 0; i < cfg.numCombs; ++i)
    {
        int delayL = static_cast<int>(cfg.combDelaysMs[i] * 0.001f * currentSampleRate);
        int delayR = static_cast<int>(cfg.combDelaysMs[i] * 0.001f * currentSampleRate * 1.03f);  // Slight stereo spread

        combsL[i].setDelay(delayL);
        combsL[i].feedback = feedback;
        combsL[i].damp = damp;

        combsR[i].setDelay(delayR);
        combsR[i].feedback = feedback;
        combsR[i].damp = damp;
    }

    for (int i = 0; i < cfg.numAllpasses; ++i)
    {
        int delayL = static_cast<int>(cfg.allpassDelaysMs[i] * 0.001f * currentSampleRate);
        int delayR = static_cast<int>(cfg.allpassDelaysMs[i] * 0.001f * currentSampleRate * 1.03f);

        allpassL[i].setDelay(juce::jmax(1, delayL));
        allpassR[i].setDelay(juce::jmax(1, delayR));
        allpassL[i].gain = 0.5f;
        allpassR[i].gain = 0.5f;
    }

    // Pre-delay
    float preMs = preDelayMs.load(std::memory_order_relaxed);
    preDelaySamples = static_cast<int>(preMs * 0.001f * currentSampleRate);
    if (!preDelayBuffer.empty())
        preDelaySamples = juce::jmin(preDelaySamples, (int)preDelayBuffer.size() - 1);
}

void ReverbProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // -----------------------------------------------------------------------
    // Audio thread — zero dynamic allocations.
    // -----------------------------------------------------------------------
    juce::ScopedNoDenormals noDenormals;

    // Re-apply parameters if mode/size/damping changed
    // (Atomic reads — no lock needed)
    applyModeParameters();

    float wet = wetParam.load(std::memory_order_relaxed);
    float dry = 1.0f - wet;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Use first channel as mono sum for reverb input; write stereo output
    for (int n = 0; n < numSamples; ++n)
    {
        float inputL = (numChannels > 0) ? buffer.getSample(0, n) : 0.0f;
        float inputR = (numChannels > 1) ? buffer.getSample(1, n) : inputL;

        // Pre-delay
        float delayedL = inputL, delayedR = inputR;
        if (preDelaySamples > 0 && !preDelayBuffer.empty())
        {
            int readPos = preDelayWrite - preDelaySamples;
            if (readPos < 0) readPos += (int)preDelayBuffer.size();
            delayedL = preDelayBuffer[static_cast<size_t>(readPos)];
            preDelayBuffer[static_cast<size_t>(preDelayWrite)] = inputL;
            preDelayWrite = (preDelayWrite + 1) % (int)preDelayBuffer.size();
            delayedR = delayedL;  // Pre-delay is mono (stereo spread comes from combs)
        }

        // Parallel comb filters
        float reverbL = 0.0f, reverbR = 0.0f;
        for (int i = 0; i < numActiveCombs; ++i)
        {
            reverbL += combsL[i].process(delayedL);
            reverbR += combsR[i].process(delayedR);
        }

        // Normalize comb output
        if (numActiveCombs > 0)
        {
            reverbL /= (float)numActiveCombs;
            reverbR /= (float)numActiveCombs;
        }

        // Series allpass diffusers
        for (int i = 0; i < numActiveAllpasses; ++i)
        {
            reverbL = allpassL[i].process(reverbL);
            reverbR = allpassR[i].process(reverbR);
        }

        // Mix wet and dry
        if (numChannels > 0)
            buffer.setSample(0, n, dry * inputL + wet * reverbL);
        if (numChannels > 1)
            buffer.setSample(1, n, dry * inputR + wet * reverbR);
    }
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void ReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void ReverbProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Reverb");
}

juce::AudioProcessorEditor* ReverbProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
