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
