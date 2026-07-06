#include "SaturatorProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

namespace
{
    constexpr float kTiltCrossoverHz = 800.0f;
    constexpr float kTiltMaxDB       = 6.0f;
    constexpr float kTubeBias        = 0.15f;
}

SaturatorProcessor::SaturatorProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "SaturatorState",
        {
            std::make_unique<juce::AudioParameterFloat> ("drive",  "Drive (dB)",   0.0f, 36.0f,  12.0f),
            std::make_unique<juce::AudioParameterChoice>("type",   "Type",         juce::StringArray { "Tape", "Tube", "Soft Clip" }, 0),
            std::make_unique<juce::AudioParameterFloat> ("tone",   "Tone",       -100.0f, 100.0f,  0.0f),
            std::make_unique<juce::AudioParameterFloat> ("mix",    "Mix (%)",      0.0f, 100.0f, 100.0f),
            std::make_unique<juce::AudioParameterFloat> ("output", "Output (dB)", -12.0f, 12.0f,   0.0f),
        })
{
    for (auto* id : { "drive", "type", "tone", "mix", "output" })
        apvts.addParameterListener (id, this);
}

void SaturatorProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if      (paramID == "drive")  driveDB.store(newValue,  std::memory_order_relaxed);
    else if (paramID == "type")   typeIndex.store((int) newValue, std::memory_order_relaxed);
    else if (paramID == "tone")   tonePct.store(newValue,  std::memory_order_relaxed);
    else if (paramID == "mix")    mixPct.store(newValue,   std::memory_order_relaxed);
    else if (paramID == "output") outputDB.store(newValue, std::memory_order_relaxed);
}

void SaturatorProcessor::setDrive(float dB)        { if (auto* p = apvts.getParameter("drive"))  p->setValueNotifyingHost (p->convertTo0to1 (dB)); }
void SaturatorProcessor::setTypeIndex(int index)   { if (auto* p = apvts.getParameter("type"))   p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); }
void SaturatorProcessor::setTone(float percent)    { if (auto* p = apvts.getParameter("tone"))   p->setValueNotifyingHost (p->convertTo0to1 (percent)); }
void SaturatorProcessor::setMix(float percent)     { if (auto* p = apvts.getParameter("mix"))    p->setValueNotifyingHost (p->convertTo0to1 (percent)); }
void SaturatorProcessor::setOutputGain(float dB)   { if (auto* p = apvts.getParameter("output")) p->setValueNotifyingHost (p->convertTo0to1 (dB)); }

void SaturatorProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    tiltLpState[0] = tiltLpState[1] = 0.0f;
    dcState[0]     = dcState[1]     = 0.0f;
    dcPrevIn[0]    = dcPrevIn[1]    = 0.0f;
}

float SaturatorProcessor::shape (float x, int type) noexcept
{
    switch (type)
    {
        case 1:   // Tube — biased tanh: asymmetry adds even harmonics
            return std::tanh (x + kTubeBias) - std::tanh (kTubeBias);

        case 2:   // Soft Clip — cubic, hard-limits at ±2/3
        {
            if (x >  1.0f) return  2.0f / 3.0f;
            if (x < -1.0f) return -2.0f / 3.0f;
            return x - (x * x * x) / 3.0f;
        }

        default:  // Tape — symmetric tanh
            return std::tanh (x);
    }
}

void SaturatorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const float drive   = std::pow (10.0f, driveDB.load(std::memory_order_relaxed) / 20.0f);
    const int   type    = typeIndex.load(std::memory_order_relaxed);
    const float tone    = tonePct.load(std::memory_order_relaxed) * 0.01f;   // -1..1
    const float mix     = mixPct.load(std::memory_order_relaxed) * 0.01f;
    const float outGain = std::pow (10.0f, outputDB.load(std::memory_order_relaxed) / 20.0f);

    // Small-signal unity gain: tanh(d·x) ≈ d·x for small x → divide by drive
    const float makeup = 1.0f / juce::jmax (1.0f, drive);

    // Tilt gains: tone > 0 = brighter (high shelf up, low shelf down)
    const float toneDB    = tone * kTiltMaxDB;
    const float lowGain   = std::pow (10.0f, -toneDB / 20.0f);
    const float highGain  = std::pow (10.0f,  toneDB / 20.0f);
    const float tiltCoeff = std::exp (-2.0f * juce::MathConstants<float>::pi
                                            * kTiltCrossoverHz / (float) sampleRate);

    // DC blocker (~10 Hz) removes the signal-dependent offset of the Tube stage
    const float dcCoeff = 1.0f - (2.0f * juce::MathConstants<float>::pi
                                       * 10.0f / (float) sampleRate);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dry = data[n];

            float wet = shape (dry * drive, type) * makeup;

            if (type == 1)
            {
                // One-pole DC blocker: y[n] = x[n] - x[n-1] + c·y[n-1]
                const float y = wet - dcPrevIn[ch] + dcCoeff * dcState[ch];
                dcPrevIn[ch] = wet;
                dcState[ch]  = y;
                wet = y;
            }

            // Post tilt: split at 800 Hz, weight the bands
            tiltLpState[ch] = tiltCoeff * tiltLpState[ch] + (1.0f - tiltCoeff) * wet;
            const float low  = tiltLpState[ch];
            const float high = wet - low;
            wet = low * lowGain + high * highGain;

            data[n] = (dry * (1.0f - mix) + wet * mix) * outGain;
        }
    }
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void SaturatorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SaturatorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void SaturatorProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Saturator");
}

juce::AudioProcessorEditor* SaturatorProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
