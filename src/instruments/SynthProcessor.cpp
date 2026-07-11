#include "SynthProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// ---------------------------------------------------------------------------
// Factory presets
// ---------------------------------------------------------------------------

namespace
{
    struct SynthPreset
    {
        const char* name;
        int   wave;                     // 0 Saw 1 Square 2 Tri 3 Sine
        float cutoff, resonance, attack, decay, sustain, release, filterEnv, volume;
    };

    // Init first (index 0) = the plain defaults. The rest are the "synth pack".
    constexpr SynthPreset kPresets[] =
    {
        { "Init",       0,  3000.0f, 0.20f, 0.005f, 0.20f, 0.75f, 0.25f, 0.40f, 0.80f },
        { "Bass",       0,   700.0f, 0.30f, 0.002f, 0.18f, 0.55f, 0.10f, 0.55f, 0.85f },
        { "Sub",        3,   250.0f, 0.05f, 0.004f, 0.30f, 0.90f, 0.12f, 0.10f, 0.90f },
        { "Lead",       0,  4500.0f, 0.45f, 0.010f, 0.30f, 0.80f, 0.30f, 0.35f, 0.75f },
        { "Pluck",      1,  2600.0f, 0.35f, 0.002f, 0.14f, 0.00f, 0.09f, 0.80f, 0.80f },
        { "Keys",       2,  2200.0f, 0.15f, 0.006f, 0.35f, 0.55f, 0.30f, 0.30f, 0.80f },
        { "Pad",        0,  1600.0f, 0.25f, 0.60f,  0.80f, 0.80f, 1.20f, 0.55f, 0.70f },
        { "Brass",      0,  2000.0f, 0.20f, 0.08f,  0.30f, 0.70f, 0.30f, 0.50f, 0.78f },
        { "Pluck Wide", 0,  3200.0f, 0.40f, 0.001f, 0.12f, 0.05f, 0.10f, 0.75f, 0.80f },
    };
}

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

// ---------------------------------------------------------------------------
// Factory presets (AudioProcessor programs)
// ---------------------------------------------------------------------------

juce::StringArray SynthProcessor::presetNames()
{
    juce::StringArray names;
    for (const auto& p : kPresets)
        names.add (p.name);
    return names;
}

int SynthProcessor::getNumPrograms() { return (int) std::size (kPresets); }

const juce::String SynthProcessor::getProgramName (int index)
{
    return (index >= 0 && index < (int) std::size (kPresets)) ? juce::String (kPresets[index].name) : juce::String();
}

void SynthProcessor::applyProgram (int index)
{
    if (index < 0 || index >= (int) std::size (kPresets))
        return;

    const auto& p = kPresets[index];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };

    set ("wave",      (float) p.wave);
    set ("cutoff",    p.cutoff);
    set ("resonance", p.resonance);
    set ("attack",    p.attack);
    set ("decay",     p.decay);
    set ("sustain",   p.sustain);
    set ("release",   p.release);
    set ("filterEnv", p.filterEnv);
    set ("volume",    p.volume);
}

void SynthProcessor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, index);
    applyProgram (currentProgram);
}

// ---------------------------------------------------------------------------
// Editor — preset picker + parameter knobs
// ---------------------------------------------------------------------------

namespace
{
    class SynthEditor : public juce::AudioProcessorEditor
    {
    public:
        SynthEditor (SynthProcessor& p, juce::AudioProcessorValueTreeState& s)
            : juce::AudioProcessorEditor (p), proc (p)
        {
            title.setText ("Aizana Synth", juce::dontSendNotification);
            title.setFont (juce::Font (16.0f, juce::Font::bold));
            addAndMakeVisible (title);

            presetLabel.setText ("Preset", juce::dontSendNotification);
            addAndMakeVisible (presetLabel);
            presetBox.addItemList (SynthProcessor::presetNames(), 1);
            presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
            presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
            addAndMakeVisible (presetBox);

            waveBox.addItemList (SynthProcessor::waveChoices(), 1);
            waveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, "wave", waveBox);
            addAndMakeVisible (waveBox);
            addLabelFor (waveBox, "Wave");

            addKnob (s, "cutoff",    "Cutoff");
            addKnob (s, "resonance", "Reso");
            addKnob (s, "attack",    "Attack");
            addKnob (s, "decay",     "Decay");
            addKnob (s, "sustain",   "Sustain");
            addKnob (s, "release",   "Release");
            addKnob (s, "filterEnv", "Flt Env");
            addKnob (s, "volume",    "Volume");

            setSize (520, 300);
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff23262b)); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            title.setBounds (r.removeFromTop (24));
            auto presetRow = r.removeFromTop (28);
            presetLabel.setBounds (presetRow.removeFromLeft (48));
            presetBox.setBounds (presetRow.removeFromLeft (200));
            auto waveRow = r.removeFromTop (28).withTrimmedTop (4);
            waveBox.setBounds (waveRow.removeFromLeft (200).withTrimmedLeft (48));
            r.removeFromTop (8);

            // knob grid (4 per row)
            const int kw = r.getWidth() / 4;
            for (int i = 0; i < knobs.size(); ++i)
            {
                if (i % 4 == 0 && i > 0) r.removeFromTop (0);
                auto cell = juce::Rectangle<int> ((i % 4) * kw, r.getY() + (i / 4) * 100, kw, 96);
                labels[i]->setBounds (cell.removeFromTop (16));
                knobs[i]->setBounds (cell);
            }
        }

    private:
        void addLabelFor (juce::Component& c, const juce::String& text)
        {
            auto* l = new juce::Label (); l->setText (text, juce::dontSendNotification);
            l->attachToComponent (&c, true);
            waveTextLabel.reset (l); addAndMakeVisible (l);
        }

        void addKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
        {
            auto* k = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            addAndMakeVisible (k);
            knobs.add (k);
            atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, id, *k));

            auto* l = new juce::Label (); l->setText (text, juce::dontSendNotification);
            l->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (l);
            labels.add (l);
        }

        SynthProcessor& proc;
        juce::Label title, presetLabel;
        juce::ComboBox presetBox, waveBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAtt;
        std::unique_ptr<juce::Label> waveTextLabel;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label>  labels;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEditor)
    };
}

juce::AudioProcessorEditor* SynthProcessor::createEditor()
{
    return new SynthEditor (*this, apvts);
}
