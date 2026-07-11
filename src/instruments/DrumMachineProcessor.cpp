#include "DrumMachineProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Synthesiser sound + voice (procedural drum synthesis)
// ---------------------------------------------------------------------------

namespace
{
    enum class Drum { Kick, Snare, ClosedHat, OpenHat, Clap, Rim, TomLo, TomHi, Crash, Cowbell };

    Drum drumForNote (int note) noexcept
    {
        switch (note)
        {
            case 35: case 36:                     return Drum::Kick;
            case 37:                              return Drum::Rim;
            case 38: case 40:                     return Drum::Snare;
            case 39:                              return Drum::Clap;
            case 42: case 44:                     return Drum::ClosedHat;
            case 46:                              return Drum::OpenHat;
            case 41: case 45: case 43:            return Drum::TomLo;
            case 47: case 48: case 50:            return Drum::TomHi;
            case 49: case 52: case 55: case 57:   return Drum::Crash;
            case 56:                              return Drum::Cowbell;
            default:                              return Drum::ClosedHat;
        }
    }

    struct DrumSound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    // One-pole coefficient for a given cutoff (used for the noise shaping filters).
    inline float poleCoeff (float hz, double sr) noexcept
    {
        return 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sr);
    }

    class DrumVoice : public juce::SynthesiserVoice
    {
    public:
        explicit DrumVoice (DrumMachineProcessor::Params& p) : params (p) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<DrumSound*> (s) != nullptr;
        }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
        {
            const double sr = getSampleRate();
            type  = drumForNote (midiNote);
            level = juce::jlimit (0.0f, 1.0f, velocity) * params.level.load (std::memory_order_relaxed);

            phase = 0.0; pitchEnv = 1.0; amp = 1.0f; clapPhase = 0.0f; sampleAge = 0;
            hp = lp = lp2 = 0.0f;

            const float kd = juce::jlimit (0.3f, 2.0f, params.kickDecay.load (std::memory_order_relaxed));
            const float hd = juce::jlimit (0.3f, 2.0f, params.hatDecay.load (std::memory_order_relaxed));
            snareToneMix   = juce::jlimit (0.0f, 1.0f, params.snareTone.load (std::memory_order_relaxed));

            auto decay = [sr] (float seconds) { return std::exp (-1.0f / ((float) seconds * (float) sr)); };

            switch (type)
            {
                case Drum::Kick:      startFreq = 118.0; baseFreq = 46.0;  pitchCoeff = decay (0.030f); ampCoeff = decay (0.34f * kd); level *= 1.15f; break;
                case Drum::Snare:     startFreq = 330.0; baseFreq = 180.0; pitchCoeff = decay (0.020f); ampCoeff = decay (0.16f);      hpC = poleCoeff (1200.0f, sr); lpC = poleCoeff (7000.0f, sr); break;
                case Drum::ClosedHat: ampCoeff = decay (0.045f * hd); hpC = poleCoeff (8000.0f, sr); break;
                case Drum::OpenHat:   ampCoeff = decay (0.34f  * hd); hpC = poleCoeff (8000.0f, sr); break;
                case Drum::Clap:      ampCoeff = decay (0.20f);       hpC = poleCoeff (1000.0f, sr); lpC = poleCoeff (3000.0f, sr); break;
                case Drum::Rim:       startFreq = 1700.0; baseFreq = 1700.0; pitchCoeff = 1.0; ampCoeff = decay (0.028f); break;
                case Drum::TomLo:     startFreq = 150.0; baseFreq = 110.0; pitchCoeff = decay (0.060f); ampCoeff = decay (0.30f); break;
                case Drum::TomHi:     startFreq = 240.0; baseFreq = 180.0; pitchCoeff = decay (0.050f); ampCoeff = decay (0.26f); break;
                case Drum::Crash:     ampCoeff = decay (1.10f); hpC = poleCoeff (6000.0f, sr); level *= 0.7f; break;
                case Drum::Cowbell:   startFreq = 540.0; baseFreq = 800.0; pitchCoeff = 1.0; ampCoeff = decay (0.30f); break;
            }
        }

        void stopNote (float, bool) override {}   // one-shots decay on their own
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
        {
            if (! active())
                return;

            const double sr = getSampleRate();
            const int    numCh = out.getNumChannels();

            for (int i = 0; i < numSamples; ++i)
            {
                float s = 0.0f;

                switch (type)
                {
                    case Drum::Kick: case Drum::TomLo: case Drum::TomHi:
                    {
                        const double f = baseFreq + (startFreq - baseFreq) * pitchEnv;
                        s = (float) std::sin (phase);
                        phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
                        pitchEnv *= pitchCoeff;
                        if (type == Drum::Kick && sampleAge < (int) (0.002 * sr))   // beater click
                            s += 0.6f * (rng.nextFloat() * 2.0f - 1.0f);
                        break;
                    }
                    case Drum::Snare:
                    {
                        const double f = baseFreq + (startFreq - baseFreq) * pitchEnv;
                        const float body = (float) std::sin (phase);
                        phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
                        pitchEnv *= pitchCoeff;
                        const float n = bandNoise();
                        s = (1.0f - snareToneMix) * body + (0.6f + 0.8f * snareToneMix) * n;
                        break;
                    }
                    case Drum::ClosedHat: case Drum::OpenHat: case Drum::Crash:
                    {
                        const float x = rng.nextFloat() * 2.0f - 1.0f;
                        lp += hpC * (x - lp);
                        s = x - lp;                              // high-pass → metallic
                        break;
                    }
                    case Drum::Clap:
                    {
                        // three quick bursts then a tail (classic clap envelope)
                        float burst = 1.0f;
                        const double t = (double) sampleAge / sr;
                        if (t < 0.030 && std::fmod (t, 0.010) > 0.005) burst = 0.15f;
                        s = bandNoise() * burst;
                        break;
                    }
                    case Drum::Rim:
                    {
                        s = (float) std::sin (phase) + 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
                        phase += 2.0 * juce::MathConstants<double>::pi * startFreq / sr;
                        break;
                    }
                    case Drum::Cowbell:
                    {
                        s = 0.5f * (float) (std::sin (phase) + std::sin (phase * (baseFreq / startFreq)));
                        phase += 2.0 * juce::MathConstants<double>::pi * startFreq / sr;
                        break;
                    }
                }

                s *= amp * level;
                amp *= ampCoeff;
                ++sampleAge;

                for (int ch = 0; ch < numCh; ++ch)
                    out.addSample (ch, startSample + i, s);

                if (amp < 1.0e-4f)
                {
                    clearCurrentNote();
                    amp = 0.0f;
                    break;
                }
            }
        }

    private:
        bool active() const { return amp > 0.0f; }

        // Two one-pole stages → crude band-pass for snare/clap noise.
        float bandNoise() noexcept
        {
            const float x = rng.nextFloat() * 2.0f - 1.0f;
            hp += hpC * (x  - hp);      // remove lows
            const float bp = x - hp;
            lp2 += lpC * (bp - lp2);    // remove highs
            return lp2;
        }

        DrumMachineProcessor::Params& params;
        Drum  type = Drum::ClosedHat;
        double phase = 0.0, startFreq = 0.0, baseFreq = 0.0, pitchEnv = 1.0, pitchCoeff = 1.0;
        float amp = 0.0f, ampCoeff = 0.0f, level = 0.0f;
        float snareToneMix = 0.5f;
        float hp = 0.0f, lp = 0.0f, lp2 = 0.0f, hpC = 0.5f, lpC = 0.5f;
        float clapPhase = 0.0f;
        int   sampleAge = 0;
        juce::Random rng;
    };
}

// ---------------------------------------------------------------------------
// DrumMachineProcessor
// ---------------------------------------------------------------------------

DrumMachineProcessor::DrumMachineProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))   // synth: no audio input
    , apvts (*this, nullptr, "DrumsState",
        {
            std::make_unique<juce::AudioParameterFloat> ("level",     "Level",       0.0f, 1.0f, 0.85f),
            std::make_unique<juce::AudioParameterFloat> ("kickDecay", "Kick Decay",  0.3f, 2.0f, 1.0f),
            std::make_unique<juce::AudioParameterFloat> ("snareTone", "Snare Tone",  0.0f, 1.0f, 0.5f),
            std::make_unique<juce::AudioParameterFloat> ("hatDecay",  "Hat Decay",   0.3f, 2.0f, 1.0f),
        })
{
    pLevel     = apvts.getRawParameterValue ("level");
    pKickDecay = apvts.getRawParameterValue ("kickDecay");
    pSnareTone = apvts.getRawParameterValue ("snareTone");
    pHatDecay  = apvts.getRawParameterValue ("hatDecay");

    synth.addSound (new DrumSound());
    for (int i = 0; i < 24; ++i)               // generous polyphony for busy patterns
        synth.addVoice (new DrumVoice (params));
}

void DrumMachineProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

void DrumMachineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Mirror the APVTS macro values into the atomics the voices read at note-on.
    params.level    .store (pLevel     != nullptr ? pLevel->load()     : 0.85f, std::memory_order_relaxed);
    params.kickDecay.store (pKickDecay != nullptr ? pKickDecay->load() : 1.0f,  std::memory_order_relaxed);
    params.snareTone.store (pSnareTone != nullptr ? pSnareTone->load() : 0.5f,  std::memory_order_relaxed);
    params.hatDecay .store (pHatDecay  != nullptr ? pHatDecay->load()  : 1.0f,  std::memory_order_relaxed);

    buffer.clear();   // instrument has no input — start from silence
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

void DrumMachineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumMachineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void DrumMachineProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Drums");
}
