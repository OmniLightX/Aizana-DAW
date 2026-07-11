// test_instruments_dsp.cpp — P0 (Big-4 gap): built-in instruments produce audio
// from MIDI. Render-proof for the drum machine + poly synth: a note-on yields
// non-silent output, no MIDI yields silence, and the synth releases to silence.
#include <catch2/catch_test_macros.hpp>
#include <instruments/DrumMachineProcessor.h>
#include <instruments/SynthProcessor.h>
#include <cmath>

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
