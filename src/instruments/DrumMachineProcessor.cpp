#include "DrumMachineProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

// ---------------------------------------------------------------------------
// Pad layout — the 8 sequencer rows and the GM note each triggers.
// ---------------------------------------------------------------------------

namespace
{
    struct PadDef { const char* name; int note; };
    constexpr PadDef kPads[DrumMachineProcessor::kNumPads] =
    {
        { "Kick",      36 },
        { "Snare",     38 },
        { "Cl Hat",    42 },
        { "Op Hat",    46 },
        { "Clap",      39 },
        { "Rim",       37 },
        { "Tom Lo",    45 },
        { "Tom Hi",    48 },
    };
}

juce::String DrumMachineProcessor::padName (int pad)
{
    return (pad >= 0 && pad < kNumPads) ? juce::String (kPads[pad].name) : juce::String();
}

int DrumMachineProcessor::padNote (int pad)
{
    return (pad >= 0 && pad < kNumPads) ? kPads[pad].note : 60;
}

bool DrumMachineProcessor::getStep (int pad, int step) const noexcept
{
    if (pad < 0 || pad >= kNumPads || step < 0 || step >= kNumSteps) return false;
    return (pattern[pad].load (std::memory_order_relaxed) & (juce::uint16) (1u << step)) != 0;
}

void DrumMachineProcessor::setStep (int pad, int step, bool on) noexcept
{
    if (pad < 0 || pad >= kNumPads || step < 0 || step >= kNumSteps) return;
    const juce::uint16 bit = (juce::uint16) (1u << step);
    auto v = pattern[pad].load (std::memory_order_relaxed);
    v = on ? (juce::uint16) (v | bit) : (juce::uint16) (v & ~bit);
    pattern[pad].store (v, std::memory_order_relaxed);
}

void DrumMachineProcessor::toggleStep (int pad, int step) noexcept
{
    setStep (pad, step, ! getStep (pad, step));
}

void DrumMachineProcessor::clearPattern() noexcept
{
    for (auto& p : pattern) p.store (0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// User samples (own kick/snare per pad)
// ---------------------------------------------------------------------------

int DrumMachineProcessor::padForNote (int note) noexcept
{
    switch (note)
    {
        case 35: case 36:            return 0;   // Kick
        case 38: case 40:            return 1;   // Snare
        case 42: case 44:            return 2;   // Closed Hat
        case 46:                     return 3;   // Open Hat
        case 39:                     return 4;   // Clap
        case 37:                     return 5;   // Rim
        case 41: case 43: case 45:   return 6;   // Tom Lo
        case 47: case 48: case 50:   return 7;   // Tom Hi
        default:                     return -1;
    }
}

bool DrumMachineProcessor::loadSample (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= kNumPads)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    auto s = std::make_unique<DrumSampleData>();
    const int chans = (int) juce::jlimit ((juce::uint32) 1, (juce::uint32) 2, reader->numChannels);
    s->buffer.setSize (chans, (int) reader->lengthInSamples);
    reader->read (&s->buffer, 0, (int) reader->lengthInSamples, 0, true, chans > 1);
    s->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    s->sourcePath   = file.getFullPathName();
    s->displayName  = file.getFileName();

    auto* raw = s.get();
    loadedSamples.add (s.release());              // owned until dtor → raw ptr stays valid (I-01)
    padSample[pad].store (raw, std::memory_order_release);
    padSamplePath[pad] = raw->sourcePath;
    return true;
}

void DrumMachineProcessor::clearSample (int pad)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    padSample[pad].store (nullptr, std::memory_order_release);   // reverts to synth voice
    padSamplePath[pad].clear();
    // The DrumSampleData stays in loadedSamples until dtor — never freed on the audio thread.
}

bool DrumMachineProcessor::padHasSample (int pad) const noexcept
{
    return pad >= 0 && pad < kNumPads && padSamplePath[pad].isNotEmpty();
}

juce::String DrumMachineProcessor::padSampleName (int pad) const
{
    return (pad >= 0 && pad < kNumPads) ? juce::File (padSamplePath[pad]).getFileName() : juce::String();
}

int DrumMachineProcessor::loadKitFolder (const juce::File& folder)
{
    // Map WAV/AIFF files in the folder onto pads by keyword in the filename.
    auto padFromName = [] (const juce::String& lower) -> int
    {
        if (lower.contains ("kick") || lower.contains ("bd") || lower.contains ("bassdrum")) return 0;
        if (lower.contains ("snare") || lower.contains ("snr") || lower.contains ("sd"))     return 1;
        if (lower.contains ("clap") || lower.contains ("clp"))                               return 4;
        if (lower.contains ("rim"))                                                          return 5;
        if ((lower.contains ("open") && lower.contains ("hat")) || lower.contains ("ohh")
            || lower.contains ("openhat"))                                                   return 3;   // before closed
        if (lower.contains ("hat") || lower.contains ("hh") || lower.contains ("chh"))       return 2;
        if (lower.contains ("tom"))                                                          return lower.contains ("hi") ? 7 : 6;
        return -1;
    };

    int count = 0;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
    {
        const int pad = padFromName (f.getFileNameWithoutExtension().toLowerCase());
        if (pad >= 0 && loadSample (pad, f))
            ++count;
    }
    return count;
}

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
        explicit DrumVoice (DrumMachineProcessor& o) : owner (o) {}

        bool canPlaySound (juce::SynthesiserSound* s) override
        {
            return dynamic_cast<DrumSound*> (s) != nullptr;
        }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
        {
            const double sr = getSampleRate();
            level = juce::jlimit (0.0f, 1.0f, velocity) * owner.voiceParams().level.load (std::memory_order_relaxed);

            // If this pad has a user sample loaded, play it as a one-shot (RT-safe:
            // padSampleRaw is an atomic ptr, the buffer is never freed while we run).
            const int pad = DrumMachineProcessor::padForNote (midiNote);
            sample = (pad >= 0) ? owner.padSampleRaw (pad) : nullptr;
            if (sample != nullptr && sample->buffer.getNumSamples() > 1)
            {
                isSampleVoice = true;
                samplePos  = 0.0;
                sampleStep = (sr > 0.0 ? sample->sourceSampleRate / sr : 1.0);
                amp = 1.0f;
                return;
            }
            isSampleVoice = false;

            type  = drumForNote (midiNote);
            phase = 0.0; pitchEnv = 1.0; amp = 1.0f; clapPhase = 0.0f; sampleAge = 0;
            hp = lp = lp2 = 0.0f;

            const float kd = juce::jlimit (0.3f, 2.0f, owner.voiceParams().kickDecay.load (std::memory_order_relaxed));
            const float hd = juce::jlimit (0.3f, 2.0f, owner.voiceParams().hatDecay.load (std::memory_order_relaxed));
            snareToneMix   = juce::jlimit (0.0f, 1.0f, owner.voiceParams().snareTone.load (std::memory_order_relaxed));

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

            const int numCh = out.getNumChannels();

            // --- user-sample playback (linear-interpolated one-shot) --------------
            if (isSampleVoice)
            {
                if (sample == nullptr) { clearCurrentNote(); amp = 0.0f; return; }
                const auto& sb  = sample->buffer;
                const int   srcN  = sb.getNumSamples();
                const int   srcCh = sb.getNumChannels();
                for (int i = 0; i < numSamples; ++i)
                {
                    const int idx = (int) samplePos;
                    if (idx >= srcN - 1) { clearCurrentNote(); amp = 0.0f; break; }
                    const float frac = (float) (samplePos - idx);
                    for (int ch = 0; ch < numCh; ++ch)
                    {
                        const float* d = sb.getReadPointer (juce::jmin (ch, srcCh - 1));
                        out.addSample (ch, startSample + i, (d[idx] + frac * (d[idx + 1] - d[idx])) * level);
                    }
                    samplePos += sampleStep;
                }
                return;
            }

            const double sr = getSampleRate();

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

        DrumMachineProcessor& owner;
        Drum  type = Drum::ClosedHat;
        double phase = 0.0, startFreq = 0.0, baseFreq = 0.0, pitchEnv = 1.0, pitchCoeff = 1.0;
        float amp = 0.0f, ampCoeff = 0.0f, level = 0.0f;
        float snareToneMix = 0.5f;
        float hp = 0.0f, lp = 0.0f, lp2 = 0.0f, hpC = 0.5f, lpC = 0.5f;
        float clapPhase = 0.0f;
        int   sampleAge = 0;
        juce::Random rng;

        // user-sample playback
        bool   isSampleVoice = false;
        DrumSampleData* sample = nullptr;
        double samplePos = 0.0, sampleStep = 1.0;
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

    formatManager.registerBasicFormats();   // WAV/AIFF for user samples

    synth.addSound (new DrumSound());
    for (int i = 0; i < 24; ++i)               // generous polyphony for busy patterns
        synth.addVoice (new DrumVoice (*this));
}

void DrumMachineProcessor::prepareToPlay (double sr, int)
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
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

    const int n = buffer.getNumSamples();

    // Merge: incoming MIDI (played notes) + audition + step-sequencer triggers.
    juce::MidiBuffer play;
    play.addEvents (midi, 0, n, 0);

    // Editor audition (message thread sets it; we consume once, lock-free).
    const int aud = auditionReq.exchange (-1, std::memory_order_relaxed);
    if (aud >= 0 && aud < kNumPads)
        play.addEvent (juce::MidiMessage::noteOn (10, padNote (aud), 1.0f), 0);

    // Transport-synced sequencer: schedule pad hits on the 16th-note grid that
    // fall inside this block, sample-accurate, driven by the host playhead.
    if (seqOn.load (std::memory_order_relaxed))
    {
        bool running = false;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (pos->getIsPlaying() && pos->getPpqPosition().hasValue() && pos->getBpm().hasValue()
                    && *pos->getBpm() > 0.0 && sampleRate > 0.0)
                {
                    running = true;
                    const double bpm   = *pos->getBpm();
                    const double ppq0  = *pos->getPpqPosition();
                    const double ppqPerSample = (bpm / 60.0) / sampleRate;
                    const double ppqEnd = ppq0 + ppqPerSample * n;
                    constexpr double stepPpq = 0.25;   // 16th notes

                    long k = (long) std::ceil (ppq0 / stepPpq - 1.0e-9);
                    for (; k * stepPpq < ppqEnd; ++k)
                    {
                        const double boundary = k * stepPpq;
                        const int offset = (int) std::llround ((boundary - ppq0) / ppqPerSample);
                        if (offset < 0 || offset >= n) continue;

                        const int step = (int) (((k % kNumSteps) + kNumSteps) % kNumSteps);
                        uiStep.store (step, std::memory_order_relaxed);

                        for (int pad = 0; pad < kNumPads; ++pad)
                            if (pattern[pad].load (std::memory_order_relaxed) & (juce::uint16) (1u << step))
                                play.addEvent (juce::MidiMessage::noteOn (10, padNote (pad), 1.0f), offset);
                    }
                }
            }
        }
        if (! running)
            uiStep.store (-1, std::memory_order_relaxed);
    }
    else
    {
        uiStep.store (-1, std::memory_order_relaxed);
    }

    buffer.clear();   // instrument has no input — start from silence
    synth.renderNextBlock (buffer, play, 0, n);
}

void DrumMachineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Persist the sequencer pattern + on/off alongside the parameters.
    juce::String pat;
    for (int i = 0; i < kNumPads; ++i)
        pat << (i ? "," : "") << juce::String::toHexString ((int) pattern[i].load (std::memory_order_relaxed));
    state.setProperty ("drumPattern", pat, nullptr);
    state.setProperty ("seqOn", seqOn.load (std::memory_order_relaxed), nullptr);

    for (int i = 0; i < kNumPads; ++i)
        state.setProperty ("padSample" + juce::String (i), padSamplePath[i], nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumMachineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (apvts.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        apvts.replaceState (tree);

        clearPattern();
        const auto toks = juce::StringArray::fromTokens (tree.getProperty ("drumPattern").toString(), ",", "");
        for (int i = 0; i < kNumPads && i < toks.size(); ++i)
            pattern[i].store ((juce::uint16) toks[i].getHexValue32(), std::memory_order_relaxed);
        seqOn.store ((bool) tree.getProperty ("seqOn", false), std::memory_order_relaxed);

        // Re-load user samples from their stored paths (message thread).
        for (int i = 0; i < kNumPads; ++i)
        {
            clearSample (i);
            const auto path = tree.getProperty ("padSample" + juce::String (i)).toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile())
                    loadSample (i, f);
            }
        }
    }
}

void DrumMachineProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana Drums");
}

// ---------------------------------------------------------------------------
// Editor — Ultrabeat-style pad grid + step sequencer
// ---------------------------------------------------------------------------

namespace
{
    class DrumEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
    {
    public:
        DrumEditor (DrumMachineProcessor& p, juce::AudioProcessorValueTreeState& s)
            : juce::AudioProcessorEditor (p), proc (p)
        {
            title.setText ("Aizana Drums", juce::dontSendNotification);
            title.setFont (juce::Font (16.0f, juce::Font::bold));
            addAndMakeVisible (title);

            seqButton.setButtonText ("Sequencer");
            seqButton.setClickingTogglesState (true);
            seqButton.setToggleState (proc.isSeqEnabled(), juce::dontSendNotification);
            seqButton.onClick = [this] { proc.setSeqEnabled (seqButton.getToggleState()); };
            addAndMakeVisible (seqButton);

            clearButton.setButtonText ("Clear");
            clearButton.onClick = [this] { proc.clearPattern(); repaint(); };
            addAndMakeVisible (clearButton);

            kitButton.setButtonText ("Kit…");
            kitButton.onClick = [this] { chooseKitFolder(); };
            addAndMakeVisible (kitButton);

            hint.setText ("Klick = Step · Name klicken = anhören · Rechtsklick/Drop = Sample", juce::dontSendNotification);
            hint.setFont (juce::Font (11.0f));
            hint.setColour (juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible (hint);

            addKnob (s, "level",     "Level");
            addKnob (s, "kickDecay", "Kick");
            addKnob (s, "snareTone", "Snare");
            addKnob (s, "hatDecay",  "Hat");

            setSize (660, 400);
            startTimerHz (30);
        }

        ~DrumEditor() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1e2126));

            const auto grid = cellBounds();
            const int cur   = proc.getCurrentStep();
            const float cellW = grid.getWidth()  / (float) DrumMachineProcessor::kNumSteps;
            const float rowH  = grid.getHeight() / (float) DrumMachineProcessor::kNumPads;

            for (int pad = 0; pad < DrumMachineProcessor::kNumPads; ++pad)
            {
                const bool sampled = proc.padHasSample (pad);
                g.setColour (sampled ? juce::Colour (0xff54d69e) : juce::Colours::lightgrey);   // teal = user sample
                g.setFont (12.0f);
                g.drawText (sampled ? (juce::String::fromUTF8 ("\xe2\x97\x8f ") + DrumMachineProcessor::padName (pad))
                                    : DrumMachineProcessor::padName (pad),
                            gridArea.getX(), (int) (grid.getY() + pad * rowH),
                            kLabelW - 6, (int) rowH, juce::Justification::centredRight);

                for (int step = 0; step < DrumMachineProcessor::kNumSteps; ++step)
                {
                    juce::Rectangle<float> cell (grid.getX() + step * cellW,
                                                 grid.getY() + pad * rowH, cellW, rowH);
                    auto c = cell.reduced (2.0f);
                    const bool on = proc.getStep (pad, step);
                    juce::Colour base = ((step / 4) % 2 == 0) ? juce::Colour (0xff2a2e35)
                                                              : juce::Colour (0xff24282f);
                    if (step == cur) base = base.brighter (0.30f);
                    g.setColour (on ? juce::Colour (0xff4aa3ff) : base);
                    g.fillRoundedRectangle (c, 3.0f);
                    if (step == cur)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.45f));
                        g.drawRoundedRectangle (c, 3.0f, 1.0f);
                    }
                }
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10);
            title.setBounds (r.removeFromTop (24));

            auto ctrl = r.removeFromBottom (98);
            gridArea = r;

            auto top = ctrl.removeFromTop (26);
            seqButton.setBounds (top.removeFromLeft (110));
            top.removeFromLeft (8);
            clearButton.setBounds (top.removeFromLeft (64));
            top.removeFromLeft (6);
            kitButton.setBounds (top.removeFromLeft (60));
            top.removeFromLeft (12);
            hint.setBounds (top);

            ctrl.removeFromTop (6);
            const int kw = ctrl.getWidth() / 4;
            for (int i = 0; i < knobs.size(); ++i)
            {
                juce::Rectangle<int> cell (ctrl.getX() + i * kw, ctrl.getY(), kw, ctrl.getHeight());
                labels[i]->setBounds (cell.removeFromTop (14));
                knobs[i]->setBounds (cell);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto grid = cellBounds();
            const float rowH = grid.getHeight() / (float) DrumMachineProcessor::kNumPads;

            // Name column → left-click audition, right-click sample menu.
            if (e.x >= gridArea.getX() && e.x < gridArea.getX() + kLabelW
                && e.y >= grid.getY() && e.y < grid.getBottom())
            {
                const int pad = (int) ((e.y - grid.getY()) / rowH);
                if (pad < 0 || pad >= DrumMachineProcessor::kNumPads)
                    return;

                if (e.mods.isPopupMenu())
                    showPadMenu (pad);
                else
                    proc.auditionPad (pad);
                return;
            }

            if (grid.contains (e.getPosition()))
            {
                const float cellW = grid.getWidth() / (float) DrumMachineProcessor::kNumSteps;
                const int step = (int) ((e.x - grid.getX()) / cellW);
                const int pad  = (int) ((e.y - grid.getY()) / rowH);
                if (pad >= 0 && pad < DrumMachineProcessor::kNumPads
                    && step >= 0 && step < DrumMachineProcessor::kNumSteps)
                {
                    proc.toggleStep (pad, step);
                    repaint();
                }
            }
        }

    private:
        static constexpr int kLabelW = 60;

        juce::Rectangle<int> cellBounds() const { return gridArea.withTrimmedLeft (kLabelW); }

        void timerCallback() override { repaint (cellBounds()); }

        void addKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& text)
        {
            auto* k = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
            addAndMakeVisible (k);
            knobs.add (k);
            atts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (s, id, *k));

            auto* l = new juce::Label(); l->setText (text, juce::dontSendNotification);
            l->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (l);
            labels.add (l);
        }

        // --- sample loading (drag-drop + context menu + kit folder) -----------
        int padRowAt (int y) const
        {
            const auto grid = cellBounds();
            if (y < grid.getY() || y >= grid.getBottom()) return -1;
            const float rowH = grid.getHeight() / (float) DrumMachineProcessor::kNumPads;
            const int pad = (int) ((y - grid.getY()) / rowH);
            return (pad >= 0 && pad < DrumMachineProcessor::kNumPads) ? pad : -1;
        }

        void showPadMenu (int pad)
        {
            juce::PopupMenu m;
            m.addItem (1, "Sample laden…  (" + DrumMachineProcessor::padName (pad) + ")");
            m.addItem (2, "Sample entfernen", proc.padHasSample (pad));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                [this, pad] (int r)
                {
                    if (r == 1) chooseSampleForPad (pad);
                    else if (r == 2) { proc.clearSample (pad); repaint(); }
                });
        }

        void chooseSampleForPad (int pad)
        {
            chooser = std::make_unique<juce::FileChooser> ("Sample für " + DrumMachineProcessor::padName (pad),
                                                           juce::File(), "*.wav;*.aif;*.aiff");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, pad] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (f.existsAsFile()) proc.loadSample (pad, f);
                    repaint();
                });
        }

        void chooseKitFolder()
        {
            chooser = std::make_unique<juce::FileChooser> ("Kit-Ordner wählen (Samples nach Namen zugeordnet)",
                                                           juce::File());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc)
                {
                    const auto dir = fc.getResult();
                    if (dir.isDirectory()) proc.loadKitFolder (dir);
                    repaint();
                });
        }

        // FileDragAndDropTarget — drop a WAV onto a pad row to load it.
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            for (const auto& f : files)
                if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff"))
                    return true;
            return false;
        }

        void filesDropped (const juce::StringArray& files, int, int y) override
        {
            const int pad = padRowAt (y);
            if (pad < 0 || files.isEmpty())
                return;
            proc.loadSample (pad, juce::File (files[0]));
            repaint();
        }

        DrumMachineProcessor& proc;
        juce::Label title, hint;
        juce::TextButton seqButton, clearButton, kitButton;
        juce::Rectangle<int> gridArea;
        juce::OwnedArray<juce::Slider> knobs;
        juce::OwnedArray<juce::Label>  labels;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> atts;
        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEditor)
    };
}

juce::AudioProcessorEditor* DrumMachineProcessor::createEditor()
{
    return new DrumEditor (*this, apvts);
}
