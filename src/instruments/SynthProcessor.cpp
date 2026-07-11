#include "SynthProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Synthesiser sound + voice (2-osc subtractive with TPT SVF low-pass)
// ---------------------------------------------------------------------------

namespace
{
    struct SynthSound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    class SynthVoice : public juce::SynthesiserVoice
    {
    public:
        explicit SynthVoice (SynthProcessor::Params& p) : params (p) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<SynthSound*> (s) != nullptr;
        }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
        {
            const double sr = getSampleRate();
            const double freq = juce::MidiMessage::getMidiNoteInHertz (midiNote);
            phase1 = 0.0; phase2 = 0.0;
            inc1 = freq / sr;
            inc2 = freq * std::pow (2.0, 7.0 / 1200.0) / sr;   // +7 cents detune
            vel  = juce::jlimit (0.05f, 1.0f, velocity);
            ic1eq = ic2eq = 0.0f;

            updateEnvParams();
            ampEnv.noteOn();
            filtEnv.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
            {
                ampEnv.noteOff();
                filtEnv.noteOff();
            }
            else
            {
                clearCurrentNote();
                ampEnv.reset();
                filtEnv.reset();
            }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
        {
            if (! ampEnv.isActive())
                return;

            const double sr    = getSampleRate();
            const int    numCh = out.getNumChannels();
            const int    wave  = params.wave.load (std::memory_order_relaxed);
            const float  baseCut = juce::jlimit (20.0f, 18000.0f, params.cutoff.load (std::memory_order_relaxed));
            const float  res     = juce::jlimit (0.0f, 0.98f, params.resonance.load (std::memory_order_relaxed));
            const float  envAmt  = juce::jlimit (0.0f, 1.0f, params.filterEnv.load (std::memory_order_relaxed));
            const float  vol     = juce::jlimit (0.0f, 1.0f, params.volume.load (std::memory_order_relaxed));
            const float  k       = 2.0f - 1.9f * res;
            updateEnvParams();

            for (int i = 0; i < numSamples; ++i)
            {
                const float osc = 0.5f * (oscValue (wave, phase1) + oscValue (wave, phase2));
                phase1 += inc1; if (phase1 >= 1.0) phase1 -= 1.0;
                phase2 += inc2; if (phase2 >= 1.0) phase2 -= 1.0;

                // Filter cutoff tracks its own envelope (upward from base).
                const float fe = filtEnv.getNextSample();
                float cut = baseCut * (1.0f + envAmt * 6.0f * fe);
                cut = juce::jlimit (20.0f, (float) (sr * 0.45), cut);

                const float g  = std::tan (juce::MathConstants<float>::pi * cut / (float) sr);
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;
                const float v3 = osc - ic2eq;
                const float v1 = a1 * ic1eq + a2 * v3;
                const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0f * v1 - ic1eq;
                ic2eq = 2.0f * v2 - ic2eq;
                const float lp = v2;

                const float a = ampEnv.getNextSample();
                const float s = lp * a * vel * vol;

                for (int ch = 0; ch < numCh; ++ch)
                    out.addSample (ch, startSample + i, s);

                if (! ampEnv.isActive())
                {
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        static float oscValue (int wave, double phase) noexcept
        {
            switch (wave)
            {
                case 1:  return phase < 0.5 ? 1.0f : -1.0f;                        // square
                case 2:  return (float) (4.0 * std::abs (phase - 0.5) - 1.0);      // triangle
                case 3:  return (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
                default: return (float) (2.0 * phase - 1.0);                       // saw
            }
        }

        void updateEnvParams()
        {
            juce::ADSR::Parameters p;
            p.attack  = juce::jmax (0.001f, params.attack.load (std::memory_order_relaxed));
            p.decay   = juce::jmax (0.001f, params.decay.load (std::memory_order_relaxed));
            p.sustain = juce::jlimit (0.0f, 1.0f, params.sustain.load (std::memory_order_relaxed));
            p.release = juce::jmax (0.001f, params.release.load (std::memory_order_relaxed));
            ampEnv.setSampleRate (getSampleRate());
            filtEnv.setSampleRate (getSampleRate());
            ampEnv.setParameters (p);
            // Filter env: snappier release so the sweep closes before the amp tail.
            juce::ADSR::Parameters fp = p; fp.sustain = 0.0f;
            filtEnv.setParameters (fp);
        }

        SynthProcessor::Params& params;
        double phase1 = 0.0, phase2 = 0.0, inc1 = 0.0, inc2 = 0.0;
        float  vel = 1.0f;
        float  ic1eq = 0.0f, ic2eq = 0.0f;
        juce::ADSR ampEnv, filtEnv;
    };
}

// ---------------------------------------------------------------------------
// SynthProcessor
// ---------------------------------------------------------------------------

SynthProcessor::SynthProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))   // synth: no audio input
    , apvts (*this, nullptr, "SynthState",
        {
            std::make_unique<juce::AudioParameterChoice>("wave",      "Wave",       waveChoices(), 0),
            std::make_unique<juce::AudioParameterFloat> ("cutoff",    "Cutoff",     juce::NormalisableRange<float> (20.0f, 18000.0f, 1.0f, 0.3f), 3000.0f),
            std::make_unique<juce::AudioParameterFloat> ("resonance", "Resonance",  0.0f, 0.98f, 0.2f),
            std::make_unique<juce::AudioParameterFloat> ("attack",    "Attack",     juce::NormalisableRange<float> (0.001f, 4.0f, 0.001f, 0.4f), 0.005f),
            std::make_unique<juce::AudioParameterFloat> ("decay",     "Decay",      juce::NormalisableRange<float> (0.001f, 4.0f, 0.001f, 0.4f), 0.20f),
            std::make_unique<juce::AudioParameterFloat> ("sustain",   "Sustain",    0.0f, 1.0f, 0.75f),
            std::make_unique<juce::AudioParameterFloat> ("release",   "Release",    juce::NormalisableRange<float> (0.001f, 6.0f, 0.001f, 0.4f), 0.25f),
            std::make_unique<juce::AudioParameterFloat> ("filterEnv", "Filter Env", 0.0f, 1.0f, 0.4f),
            std::make_unique<juce::AudioParameterFloat> ("volume",    "Volume",     0.0f, 1.0f, 0.8f),
        })
{
    synth.addSound (new SynthSound());
    for (int i = 0; i < 16; ++i)
        synth.addVoice (new SynthVoice (params));
}

void SynthProcessor::syncParams()
{
    auto get = [this] (const char* id, float fb) { auto* p = apvts.getRawParameterValue (id); return p != nullptr ? p->load() : fb; };
    params.wave     .store ((int) get ("wave", 0.0f),        std::memory_order_relaxed);
    params.cutoff   .store (get ("cutoff",    3000.0f),      std::memory_order_relaxed);
    params.resonance.store (get ("resonance", 0.2f),         std::memory_order_relaxed);
    params.attack   .store (get ("attack",    0.005f),       std::memory_order_relaxed);
    params.decay    .store (get ("decay",     0.20f),        std::memory_order_relaxed);
    params.sustain  .store (get ("sustain",   0.75f),        std::memory_order_relaxed);
    params.release  .store (get ("release",   0.25f),        std::memory_order_relaxed);
    params.filterEnv.store (get ("filterEnv", 0.4f),         std::memory_order_relaxed);
    params.volume   .store (get ("volume",    0.8f),         std::memory_order_relaxed);
}

void SynthProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

void SynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    syncParams();
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

void SynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void SynthProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Synth");
}
