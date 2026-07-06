#include "DelayProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

namespace
{
    constexpr double kMaxDelaySeconds = 4.0;

    float onePoleCoeff (float hz, double sr) noexcept
    {
        return std::exp (-2.0f * juce::MathConstants<float>::pi
                                * hz / static_cast<float> (sr));
    }
}

juce::StringArray DelayProcessor::syncChoices()
{
    return { "Frei (ms)", "1/1", "1/2", "1/2.", "1/4", "1/4.", "1/4T",
             "1/8", "1/8.", "1/8T", "1/16" };
}

double DelayProcessor::beatsForSyncIndex (int index) noexcept
{
    switch (index)
    {
        case 1:  return 4.0;         // 1/1
        case 2:  return 2.0;         // 1/2
        case 3:  return 3.0;         // 1/2.
        case 4:  return 1.0;         // 1/4
        case 5:  return 1.5;         // 1/4.
        case 6:  return 2.0 / 3.0;   // 1/4T
        case 7:  return 0.5;         // 1/8
        case 8:  return 0.75;        // 1/8.
        case 9:  return 1.0 / 3.0;   // 1/8T
        case 10: return 0.25;        // 1/16
        default: return 0.0;         // free running
    }
}

DelayProcessor::DelayProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "DelayState",
        {
            std::make_unique<juce::AudioParameterFloat> ("time",     "Time (ms)",     1.0f, 2000.0f, 350.0f),
            std::make_unique<juce::AudioParameterChoice>("sync",     "Sync",          syncChoices(), 0),
            std::make_unique<juce::AudioParameterFloat> ("feedback", "Feedback (%)",  0.0f, 95.0f,   35.0f),
            std::make_unique<juce::AudioParameterFloat> ("hpf",      "Loop HP (Hz)", 20.0f, 1000.0f, 90.0f),
            std::make_unique<juce::AudioParameterFloat> ("lpf",      "Loop LP (Hz)", 1000.0f, 20000.0f, 12000.0f),
            std::make_unique<juce::AudioParameterFloat> ("mix",      "Mix (%)",       0.0f, 100.0f,  25.0f),
            std::make_unique<juce::AudioParameterBool>  ("pingpong", "Ping-Pong",     false),
        })
{
    for (auto* id : { "time", "sync", "feedback", "hpf", "lpf", "mix", "pingpong" })
        apvts.addParameterListener (id, this);
}

void DelayProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if      (paramID == "time")     timeMs.store(newValue,      std::memory_order_relaxed);
    else if (paramID == "sync")     syncIndex.store((int) newValue, std::memory_order_relaxed);
    else if (paramID == "feedback") feedbackPct.store(newValue, std::memory_order_relaxed);
    else if (paramID == "hpf")      hpfHz.store(newValue,       std::memory_order_relaxed);
    else if (paramID == "lpf")      lpfHz.store(newValue,       std::memory_order_relaxed);
    else if (paramID == "mix")      mixPct.store(newValue,      std::memory_order_relaxed);
    else if (paramID == "pingpong") pingPong.store(newValue >= 0.5f, std::memory_order_relaxed);
}

void DelayProcessor::setTimeMs(float ms)        { if (auto* p = apvts.getParameter("time"))     p->setValueNotifyingHost (p->convertTo0to1 (ms)); }
void DelayProcessor::setSyncIndex(int index)    { if (auto* p = apvts.getParameter("sync"))     p->setValueNotifyingHost (p->convertTo0to1 ((float) index)); }
void DelayProcessor::setFeedback(float percent) { if (auto* p = apvts.getParameter("feedback")) p->setValueNotifyingHost (p->convertTo0to1 (percent)); }
void DelayProcessor::setMix(float percent)      { if (auto* p = apvts.getParameter("mix"))      p->setValueNotifyingHost (p->convertTo0to1 (percent)); }
void DelayProcessor::setPingPong(bool enabled)  { if (auto* p = apvts.getParameter("pingpong")) p->setValueNotifyingHost (enabled ? 1.0f : 0.0f); }

void DelayProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    lineLength = juce::jmax (1, (int) std::ceil (kMaxDelaySeconds * sr) + 1);

    for (auto& line : delayLine)
    {
        line.assign ((size_t) lineLength, 0.0f);
    }

    writePos = 0;
    smoothedDelaySamples = currentDelaySamplesTarget();
    fbLpState[0] = fbLpState[1] = 0.0f;
    fbHpState[0] = fbHpState[1] = 0.0f;
}

float DelayProcessor::currentDelaySamplesTarget() noexcept
{
    const int sync = syncIndex.load (std::memory_order_relaxed);
    double ms = (double) timeMs.load (std::memory_order_relaxed);

    if (const double beats = beatsForSyncIndex (sync); beats > 0.0)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto hostBpm = pos->getBpm())
                    bpm = *hostBpm;

        ms = beats * 60000.0 / juce::jmax (20.0, bpm);
    }

    const double samples = ms * 0.001 * sampleRate;
    return (float) juce::jlimit (1.0, (double) (lineLength - 2), samples);
}

void DelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (lineLength <= 2)
        return;   // not prepared → pass through

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    const float fb      = feedbackPct.load(std::memory_order_relaxed) * 0.01f;
    const float mix     = mixPct.load(std::memory_order_relaxed) * 0.01f;
    const bool  pp      = pingPong.load(std::memory_order_relaxed) && numChannels > 1;
    const float lpCoeff = onePoleCoeff (lpfHz.load(std::memory_order_relaxed), sampleRate);
    const float hpCoeff = onePoleCoeff (hpfHz.load(std::memory_order_relaxed), sampleRate);
    const float target  = currentDelaySamplesTarget();

    // ~10 ms glide on the delay time keeps time changes click-free
    const float smoothCoeff = std::exp (-1.0f / (0.010f * (float) sampleRate));

    for (int n = 0; n < numSamples; ++n)
    {
        smoothedDelaySamples = smoothCoeff * smoothedDelaySamples
                             + (1.0f - smoothCoeff) * target;

        const float readOffset = smoothedDelaySamples;
        float delayed[2] { 0.0f, 0.0f };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Linear-interpolated read behind the write head
            float readPos = (float) writePos - readOffset;
            while (readPos < 0.0f)
                readPos += (float) lineLength;

            const int   i0   = (int) readPos;
            const int   i1   = (i0 + 1) % lineLength;
            const float frac = readPos - (float) i0;

            delayed[ch] = delayLine[ch][(size_t) i0] * (1.0f - frac)
                        + delayLine[ch][(size_t) i1] * frac;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Ping-pong bounces the repeats between channels
            const int   src = pp ? (1 - ch) : ch;
            const float wet = delayed[src];

            // Feedback path: one-pole LP + HP shape the repeats
            fbLpState[ch] = lpCoeff * fbLpState[ch] + (1.0f - lpCoeff) * wet;
            float shaped  = fbLpState[ch];
            fbHpState[ch] = hpCoeff * fbHpState[ch] + (1.0f - hpCoeff) * shaped;
            shaped       -= fbHpState[ch];

            const float in = buffer.getSample (ch, n);
            delayLine[ch][(size_t) writePos] = in + shaped * fb;

            buffer.setSample (ch, n, in * (1.0f - mix) + wet * mix);
        }

        writePos = (writePos + 1) % lineLength;
    }
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void DelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void DelayProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Delay");
}

juce::AudioProcessorEditor* DelayProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
