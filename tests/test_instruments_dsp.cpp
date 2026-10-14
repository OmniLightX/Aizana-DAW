// test_instruments_dsp.cpp — P0 (Big-4 gap): built-in instruments produce audio
// from MIDI. Render-proof for the drum machine + poly synth: a note-on yields
// non-silent output, no MIDI yields silence, and the synth releases to silence.
#include <catch2/catch_test_macros.hpp>
#include <instruments/DrumMachineProcessor.h>
#include <instruments/SynthProcessor.h>
#include <instruments/InstrumentOutput.h>
#include <cmath>
#include <vector>

namespace
{
    constexpr double kSR = 44100.0;
    constexpr int    kBlock = 512;

    // Render `blocks` blocks; fire one note-on at block 0 (if note >= 0) and an
    // optional note-off at `noteOffBlock`. Returns the peak magnitude seen.
    float renderPeak (juce::AudioProcessor& p, int note, float vel,
                      int blocks, int noteOffBlock = -1)
    {
        p.prepareToPlay (kSR, kBlock);
        float peak = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, kBlock);
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0 && note >= 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
            if (b == noteOffBlock && note >= 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);

            p.processBlock (buf, midi);
            peak = std::max (peak, buf.getMagnitude (0, kBlock));
        }
        return peak;
    }

    // Minimal transport for the drum sequencer: reports playing at a fixed BPM/ppq.
    struct FakePlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool   playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setPpqPosition (ppq);
            p.setBpm (bpm);
            p.setIsPlaying (playing);
            return p;
        }
    };

    // Write a constant-amplitude mono WAV to temp for the sampler tests.
    juce::File makeTempWav (float amp, int numSamples)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("aizana_drum_smp_"
                                    + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) + ".wav");
        juce::AudioBuffer<float> buf (1, numSamples);
        for (int i = 0; i < numSamples; ++i) buf.setSample (0, i, amp);

        juce::WavAudioFormat fmt;
        if (auto os = f.createOutputStream())
            if (std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.release(), 44100.0, 1, 16, {}, 0));
                w != nullptr)
                w->writeFromAudioSampleBuffer (buf, 0, numSamples);
        return f;
    }
}

TEST_CASE ("Aizana Drums: kick and hat notes produce audio", "[instrument][drums]")
{
    DrumMachineProcessor drums;
    // Kick (GM 36) is a strong transient — should be clearly audible.
    CHECK (renderPeak (drums, 36, 1.0f, 8) > 0.1f);
    // Closed hat (GM 42) is quieter/higher but still non-silent.
    CHECK (renderPeak (drums, 42, 1.0f, 8) > 0.01f);
    // Snare (GM 38) too.
    CHECK (renderPeak (drums, 38, 1.0f, 8) > 0.02f);
}

TEST_CASE ("Aizana Drums: no MIDI → silence", "[instrument][drums]")
{
    DrumMachineProcessor drums;
    CHECK (renderPeak (drums, -1, 0.0f, 8) < 1.0e-6f);
}

TEST_CASE ("Aizana Drums: velocity scales level", "[instrument][drums]")
{
    DrumMachineProcessor soft, loud;
    const float softPeak = renderPeak (soft, 36, 0.2f, 8);
    const float loudPeak = renderPeak (loud, 36, 1.0f, 8);
    CHECK (loudPeak > softPeak);
}

TEST_CASE ("Aizana Synth: held note produces sustained audio", "[instrument][synth]")
{
    SynthProcessor synth;
    // No note-off: after the attack the note sustains → clearly non-silent.
    CHECK (renderPeak (synth, 60, 1.0f, 16) > 0.05f);
}

TEST_CASE ("Aizana Synth: no MIDI → silence", "[instrument][synth]")
{
    SynthProcessor synth;
    CHECK (renderPeak (synth, -1, 0.0f, 16) < 1.0e-6f);
}

TEST_CASE ("Aizana Synth: note-off releases to silence", "[instrument][synth]")
{
    SynthProcessor synth;
    // Note on at block 0, off at block 2; with the default short release the
    // tail must fall to near-silence well before the end of a long render.
    synth.prepareToPlay (kSR, kBlock);
    float tailPeak = 0.0f;
    const int offBlock = 2;
    const int totalBlocks = 200;   // ~2.3 s — past attack/decay/release

    for (int b = 0; b < totalBlocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0)        midi.addEvent (juce::MidiMessage::noteOn  (1, 60, 1.0f), 0);
        if (b == offBlock) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        synth.processBlock (buf, midi);
        if (b >= totalBlocks - 10)     // only the last ~120 ms
            tailPeak = std::max (tailPeak, buf.getMagnitude (0, kBlock));
    }
    CHECK (tailPeak < 1.0e-3f);
}

TEST_CASE ("Aizana Synth: factory presets load and render", "[instrument][synth][preset]")
{
    SynthProcessor synth;
    REQUIRE (synth.getNumPrograms() >= 6);          // Init + the synth pack

    int bass = -1;
    for (int i = 0; i < synth.getNumPrograms(); ++i)
        if (synth.getProgramName (i) == "Bass")
            bass = i;
    REQUIRE (bass >= 0);

    synth.setCurrentProgram (bass);
    CHECK (synth.getCurrentProgram() == bass);
    // A low note through the Bass preset is clearly audible.
    CHECK (renderPeak (synth, 40, 1.0f, 16) > 0.05f);
}

TEST_CASE ("Aizana Drums: step sequencer triggers pads on the grid", "[instrument][drums][seq]")
{
    DrumMachineProcessor drums;
    drums.setStep (0, 0, true);        // kick on step 1 (index 0)
    drums.setSeqEnabled (true);
    drums.prepareToPlay (kSR, kBlock);

    FakePlayHead ph;                   // playing, 120 BPM, ppq = 0 → downbeat at block start
    drums.setPlayHead (&ph);

    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer empty;
    drums.processBlock (buf, empty);
    CHECK (buf.getMagnitude (0, kBlock) > 0.1f);      // kick fired from the pattern
    CHECK (drums.getCurrentStep() == 0);
    drums.setPlayHead (nullptr);       // drop the stack playhead before it goes out of scope
}

TEST_CASE ("Aizana Drums: sequencer with empty pattern stays silent", "[instrument][drums][seq]")
{
    DrumMachineProcessor drums;
    drums.clearPattern();
    drums.setSeqEnabled (true);
    drums.prepareToPlay (kSR, kBlock);
    FakePlayHead ph;
    drums.setPlayHead (&ph);

    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer empty;
    drums.processBlock (buf, empty);
    CHECK (buf.getMagnitude (0, kBlock) < 1.0e-6f);
    drums.setPlayHead (nullptr);
}

TEST_CASE ("Aizana Drums: audition triggers a pad without transport", "[instrument][drums]")
{
    DrumMachineProcessor drums;
    drums.prepareToPlay (kSR, kBlock);
    drums.auditionPad (0);   // kick, no playhead needed

    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer empty;
    drums.processBlock (buf, empty);
    CHECK (buf.getMagnitude (0, kBlock) > 0.1f);
}

TEST_CASE ("Aizana Drums: pattern round-trips through state", "[instrument][drums]")
{
    DrumMachineProcessor a;
    a.setStep (0, 4, true);
    a.setStep (2, 8, true);
    a.setSeqEnabled (true);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    DrumMachineProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (b.getStep (0, 4));
    CHECK (b.getStep (2, 8));
    CHECK_FALSE (b.getStep (0, 5));
    CHECK (b.isSeqEnabled());
}

TEST_CASE ("Aizana Drums: loads a WAV onto a pad and plays it", "[instrument][drums][sample]")
{
    auto wav = makeTempWav (0.5f, 4410);
    DrumMachineProcessor drums;
    REQUIRE (drums.loadSample (0, wav));      // pad 0 = Kick
    CHECK (drums.padHasSample (0));

    drums.prepareToPlay (kSR, kBlock);
    drums.auditionPad (0);
    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer m;
    drums.processBlock (buf, m);
    CHECK (buf.getMagnitude (0, kBlock) > 0.2f);   // the loaded 0.5 sample plays
    wav.deleteFile();
}

TEST_CASE ("Aizana Drums: loaded sample survives state round-trip", "[instrument][drums][sample]")
{
    auto wav = makeTempWav (0.5f, 2205);
    DrumMachineProcessor a;
    REQUIRE (a.loadSample (1, wav));           // Snare

    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    DrumMachineProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (b.padHasSample (1));                 // reloaded from the stored path
    wav.deleteFile();
}

TEST_CASE ("Aizana Drums: clearSample reverts a pad to the synth voice", "[instrument][drums][sample]")
{
    auto wav = makeTempWav (0.5f, 2205);
    DrumMachineProcessor drums;
    drums.loadSample (0, wav);
    CHECK (drums.padHasSample (0));

    drums.clearSample (0);
    CHECK_FALSE (drums.padHasSample (0));

    drums.prepareToPlay (kSR, kBlock);
    drums.auditionPad (0);
    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer m;
    drums.processBlock (buf, m);
    CHECK (buf.getMagnitude (0, kBlock) > 0.05f);   // synth kick is back
    wav.deleteFile();
}

TEST_CASE ("Aizana Drums: step sequencer triggers a loaded sample", "[instrument][drums][sample][seq]")
{
    auto wav = makeTempWav (0.5f, 4410);
    DrumMachineProcessor drums;
    drums.loadSample (0, wav);
    drums.setStep (0, 0, true);
    drums.setSeqEnabled (true);
    drums.prepareToPlay (kSR, kBlock);

    FakePlayHead ph;
    drums.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, kBlock);
    buf.clear();
    juce::MidiBuffer empty;
    drums.processBlock (buf, empty);
    CHECK (buf.getMagnitude (0, kBlock) > 0.2f);    // sampled kick fired from the pattern
    drums.setPlayHead (nullptr);
    wav.deleteFile();
}

// ---------------------------------------------------------------------------
// Gain-Staging (Regression, 2026-08-10)
//
// Befund beim Vorbereiten des Live-Testlaufs: die Instrumente lieferten mit
// Werkseinstellungen Pegel weit ueber Vollaussteuerung — eine EINZELNE Kick-Note
// stand bei 1.458 (+3,2 dB), ein Vierklang am Synth bei bis zu 2.52 (+8 dB).
// Ein frisch geladenes Instrument verzerrte also beim ersten Ton.
//
// Gefunden wurde das NICHT von den Tests, obwohl es sie gab: alle pruefen nur
// Untergrenzen ("nicht still") und relative Aussagen ("laut > leise"). Eine
// OBERGRENZE prueft keiner — genau deshalb stehen die folgenden Faelle hier.
// ---------------------------------------------------------------------------

namespace
{
    // Rendert gleichzeitig anliegende Noten und liefert den Spitzenpegel.
    float renderStackPeak (juce::AudioProcessor& p, const std::vector<int>& notes,
                           float vel, int blocks)
    {
        p.prepareToPlay (kSR, kBlock);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, kBlock);
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                for (int n : notes)
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, vel), 0);
            p.processBlock (buf, midi);
            peak = std::max (peak, buf.getMagnitude (0, kBlock));
        }
        return peak;
    }
}

TEST_CASE ("Aizana Drums: kein Pad uebersteuert bei voller Velocity", "[instrument][drums][gain]")
{
    for (int pad = 0; pad < DrumMachineProcessor::kNumPads; ++pad)
    {
        DrumMachineProcessor drums;
        const float peak = renderPeak (drums, DrumMachineProcessor::padNote (pad), 1.0f, 64);
        INFO ("Pad " << pad << " (" << DrumMachineProcessor::padName (pad) << ") Peak " << peak);
        CHECK (peak > 0.005f);      // klingt ueberhaupt
        CHECK (peak < 1.0f);        // und uebersteuert nicht
    }
}

TEST_CASE ("Aizana Drums: gleichzeitige Pads bleiben unter Vollaussteuerung", "[instrument][drums][gain]")
{
    // Kick + Snare + beide Hats + Clap auf demselben Schlag: mehr passiert in
    // einem echten Pattern nicht gleichzeitig.
    DrumMachineProcessor drums;
    std::vector<int> notes;
    for (int pad = 0; pad < 5; ++pad)
        notes.push_back (DrumMachineProcessor::padNote (pad));

    const float peak = renderStackPeak (drums, notes, 1.0f, 64);
    INFO ("5 Pads gleichzeitig, Peak " << peak);
    CHECK (peak < 1.0f);
}

TEST_CASE ("Aizana Synth: Einzelnote und Akkord bleiben unter Vollaussteuerung",
           "[instrument][synth][gain]")
{
    SynthProcessor probe;
    for (int i = 0; i < probe.getNumPrograms(); ++i)
    {
        SynthProcessor single, chord;
        single.setCurrentProgram (i);
        chord .setCurrentProgram (i);

        const float onePeak  = renderStackPeak (single, { 60 }, 0.8f, 64);
        const float fourPeak = renderStackPeak (chord, { 48, 55, 60, 64 }, 0.8f, 64);

        INFO ("Preset " << probe.getProgramName (i) << ": 1 Note " << onePeak
              << ", 4 Noten " << fourPeak);
        CHECK (onePeak  > 0.02f);     // jedes Preset klingt
        CHECK (onePeak  < 1.0f);
        CHECK (fourPeak < 1.0f);      // der Vierklang war der eigentliche Ausreisser
        CHECK (fourPeak > onePeak);   // Polyphonie summiert weiterhin hoerbar
    }
}

TEST_CASE ("Instrument-Ausgangsstufe: weiche Begrenzung", "[instrument][gain]")
{
    using namespace aizana::instrument;

    // Unterhalb der Schwelle voellig transparent — kein Faerben leiser Signale.
    for (float v : { 0.0f, 0.1f, 0.35f, 0.5f, 0.69f })
    {
        CHECK (softClip (v)  == v);
        CHECK (softClip (-v) == -v);
    }

    // Darueber monoton steigend, aber nie bis zur Vollaussteuerung.
    CHECK (softClip (0.8f) > 0.7f);
    CHECK (softClip (2.5f) < 1.0f);
    CHECK (softClip (50.0f) < 1.0f);
    CHECK (softClip (2.5f) > softClip (1.2f));

    // Symmetrisch: kein DC-Versatz durch die Begrenzung.
    CHECK (softClip (-2.5f) == -softClip (2.5f));
}

// B3: compare actual samples, not merely parameter values or non-zero peaks.
namespace
{
    using SynthSettings = std::vector<std::pair<juce::String, float>>;
    std::vector<float> renderSynthProbe (const SynthSettings& settings,
                                         std::vector<int> notes = {60})
    {
        SynthProcessor p;
        for (const auto& setting : settings)
        {
            bool found = false;
            for (auto* parameter : p.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
                    ranged != nullptr && ranged->paramID == setting.first)
                {
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (setting.second));
                    found = true;
                }
            REQUIRE (found);
        }
        p.prepareToPlay (kSR, kBlock);
        std::vector<float> result;
        for (int block = 0; block < 180; ++block)
        {
            juce::AudioBuffer<float> audio (2, kBlock);
            juce::MidiBuffer midi;
            for (int note : notes)
            {
                if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.4f), 0);
                if (block == 90) midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            }
            p.processBlock (audio, midi);
            result.insert (result.end(), audio.getReadPointer (0), audio.getReadPointer (0) + kBlock);
        }
        return result;
    }
    double probeRms (const std::vector<float>& audio, int firstBlock, int lastBlock)
    {
        double energy = 0;
        for (int i = firstBlock * kBlock; i < lastBlock * kBlock; ++i)
            energy += audio[(size_t) i] * audio[(size_t) i];
        return std::sqrt (energy / ((lastBlock - firstBlock) * kBlock));
    }
}

TEST_CASE ("Synth B3: every sound control changes rendered samples", "[synth][b3]")
{
    const auto baseline = renderSynthProbe ({});
    const SynthSettings variants {{"wave",1}, {"wave",2}, {"wave",3},
        {"cutoff",300}, {"resonance",0.8f}, {"attack",0.4f}, {"decay",0.8f},
        {"sustain",0.2f}, {"release",0.8f}, {"filterEnv",0}, {"volume",0.4f}};
    for (const auto& setting : variants)
    {
        INFO (setting.first << "=" << setting.second);
        const auto changed = renderSynthProbe ({setting});
        double difference = 0;
        bool finite = true;
        for (size_t i = 0; i < changed.size(); ++i)
        {
            finite = finite && std::isfinite (changed[i]);
            difference += std::abs (changed[i] - baseline[i]);
        }
        CHECK (finite);
        CHECK (difference / changed.size() > 1.0e-4);
    }
}

TEST_CASE ("Synth B3: attack and release shape the measured envelope", "[synth][b3]")
{
    const auto fast = renderSynthProbe ({{"attack",0.001f},{"release",0.01f}});
    const auto slow = renderSynthProbe ({{"attack",0.6f},{"release",0.8f}});
    CHECK (probeRms (slow, 0, 5) < probeRms (fast, 0, 5) * 0.3);
    CHECK (probeRms (fast, 100, 110) < 1.0e-6);
    CHECK (probeRms (slow, 100, 110) > 0.001);
    CHECK (probeRms (slow, 170, 180) < 1.0e-6);
}

TEST_CASE ("Synth B3: chord retains all four independent voices", "[synth][b3]")
{
    const SynthSettings quiet {{"volume",0.1f},{"filterEnv",0}};
    const std::vector<int> notes {48,55,60,64};
    const auto chord = renderSynthProbe (quiet, notes);
    std::vector<float> sum (chord.size());
    for (int note : notes)
    {
        const auto voice = renderSynthProbe (quiet, {note});
        CHECK (probeRms (voice, 20, 80) > 0.001);
        for (size_t i = 0; i < sum.size(); ++i) sum[i] += voice[i];
    }
    float error = 0;
    bool finite = true;
    for (size_t i = 0; i < sum.size(); ++i)
    {
        finite = finite && std::isfinite (sum[i]) && std::isfinite (chord[i]);
        error = std::max (error, std::abs (sum[i] - chord[i]));
    }
    CHECK (finite);
    CHECK (error < 1.0e-5f);
}
