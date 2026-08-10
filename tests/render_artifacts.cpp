// render_artifacts.cpp — Hör-Pakete für den Live-Testlauf.
//
// Diese Datei enthält KEINE Assertions über Klangqualität — sie rendert die
// Built-ins in echte WAV-Dateien, damit ein Mensch sie beurteilen kann, ohne
// erst eine Session bauen zu müssen. Was maschinell entscheidbar ist (Stille,
// Timing, Pegel), wird trotzdem geprüft und in befund.txt geschrieben.
//
// ALLE Cases tragen das versteckte Tag "[.artifacts]" → sie laufen NICHT im
// normalen Suite-Lauf (Catch2 blendet Tags mit führendem Punkt aus). Aufruf:
//
//     engine_tests "[artifacts]"
//
// Zielverzeichnis: $AIZANA_ARTIFACT_DIR, sonst ~/Desktop/Aizana Testläufe/render.
#include <catch2/catch_test_macros.hpp>
#include <instruments/DrumMachineProcessor.h>
#include <instruments/SynthProcessor.h>
#include <cmath>

namespace
{
    constexpr double kSR    = 44100.0;
    constexpr int    kBlock = 512;
    constexpr double kBpm   = 120.0;

    // Ein 16tel bei 120 BPM = 0,125 s. Alle Muster unten rechnen in 16teln.
    constexpr double kStepSeconds = 60.0 / kBpm / 4.0;
    const int        kStepSamples = (int) std::lround (kStepSeconds * kSR);

    juce::File artifactDir()
    {
        if (auto env = juce::SystemStats::getEnvironmentVariable ("AIZANA_ARTIFACT_DIR", {});
            env.isNotEmpty())
        {
            juce::File d (env);
            d.createDirectory();
            return d;
        }

        auto d = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                     .getChildFile ("Aizana Testlaeufe").getChildFile ("render");
        d.createDirectory();
        return d;
    }

    // Ein zeitgestempeltes MIDI-Ereignis in Sample-Auflösung.
    struct Event { int sample; juce::MidiMessage msg; };

    // Rendert `p` über die volle Länge und schreibt eine 24-bit-Stereo-WAV.
    // Gibt den Spitzenpegel zurück (0 = Stille → Silent-Failure sofort sichtbar).
    float renderToWav (juce::AudioProcessor& p, const std::vector<Event>& events,
                       double seconds, const juce::File& out,
                       juce::AudioPlayHead* playHead = nullptr)
    {
        p.setPlayHead (playHead);
        p.prepareToPlay (kSR, kBlock);

        const int total = (int) std::lround (seconds * kSR);
        juce::AudioBuffer<float> mix (2, total);
        mix.clear();

        float peak = 0.0f;
        for (int start = 0; start < total; start += kBlock)
        {
            const int n = std::min (kBlock, total - start);
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();

            juce::MidiBuffer midi;
            for (const auto& e : events)
                if (e.sample >= start && e.sample < start + n)
                    midi.addEvent (e.msg, e.sample - start);

            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                mix.copyFrom (ch, start, buf, ch, 0, n);
            peak = std::max (peak, buf.getMagnitude (0, n));
        }

        // Der PlayHead ist gleich weg — hängenden Zeiger vermeiden (I-01/UAF-Klasse,
        // exakt der Bug, der 2026-07-11 den Warp-Render zu Stille machte).
        p.setPlayHead (nullptr);

        out.deleteFile();
        juce::WavAudioFormat fmt;
        if (auto os = out.createOutputStream())
            if (std::unique_ptr<juce::AudioFormatWriter> w (
                    fmt.createWriterFor (os.release(), kSR, 2, 24, {}, 0)); w != nullptr)
                w->writeFromAudioSampleBuffer (mix, 0, total);

        return peak;
    }

    int padByName (const juce::String& name)
    {
        for (int i = 0; i < DrumMachineProcessor::kNumPads; ++i)
            if (DrumMachineProcessor::padName (i).containsIgnoreCase (name))
                return i;
        return -1;
    }

    // Transport-Stub für den Step-Sequencer: läuft mit konstantem Tempo und
    // schiebt die ppq-Position pro Block weiter — wie ein echter Host.
    struct MovingPlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setPpqPosition (ppq);
            p.setBpm (kBpm);
            p.setIsPlaying (true);
            return p;
        }
    };

    void appendBefund (const juce::File& dir, const juce::String& line)
    {
        dir.getChildFile ("befund.txt").appendText (line + "\n");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// P0-L Drums — derselbe Beat zweimal: einmal von MIDI-Noten getrieben
// (= du zeichnest Noten im PianoRoll), einmal vom Step-Sequencer
// (= P0b-L). Beide müssen gleich klingen und tight sitzen.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Artefakt: Drums - Beat aus MIDI-Noten", "[.artifacts][drums]")
{
    auto dir = artifactDir().getChildFile ("01_drums");
    dir.createDirectory();

    const int kick  = padByName ("Kick");
    const int snare = padByName ("Snare");
    const int hat   = padByName ("Hat");
    REQUIRE (kick >= 0);
    REQUIRE (snare >= 0);
    REQUIRE (hat >= 0);

    // 2 Takte à 16 Steps, klassisches Backbeat-Muster.
    const std::vector<int> kickSteps  { 0, 10, 16, 22, 26 };
    const std::vector<int> snareSteps { 4, 12, 20, 28 };

    std::vector<Event> ev;
    auto addHit = [&ev] (int pad, int step, float vel)
    {
        const int note = DrumMachineProcessor::padNote (pad);
        ev.push_back ({ step * kStepSamples, juce::MidiMessage::noteOn (1, note, vel) });
        ev.push_back ({ step * kStepSamples + 200, juce::MidiMessage::noteOff (1, note) });
    };

    for (int s : kickSteps)  addHit (kick,  s, 1.0f);
    for (int s : snareSteps) addHit (snare, s, 0.9f);
    for (int s = 0; s < 32; s += 2)                       // Achtel-Hats, Offbeats leiser
        addHit (hat, s, (s % 4 == 0) ? 0.8f : 0.5f);

    DrumMachineProcessor drums;
    const float peak = renderToWav (drums, ev, 4.5, dir.getChildFile ("a_beat_midi.wav"));

    CHECK (peak > 0.1f);                                   // nicht still
    appendBefund (dir, "a_beat_midi.wav  - Beat aus MIDI-Noten, 120 BPM, 2 Takte. Peak "
                       + juce::String (peak, 3)
                       + ". Erwartung: Kick/Snare/HiHat klar unterscheidbar, Timing tight.");
}

// Sidestick-Variante: derselbe Groove, aber mit Rim statt Snare. Der Rim lag
// vorher nur ~1 dB unter der Kick (kein Pegelfaktor) und dominierte damit den
// Beat; seit 2026-08-10 sitzt er rund 10 dB darunter, also auf Akzent-Hoehe.
TEST_CASE ("Artefakt: Drums - Beat mit Sidestick statt Snare", "[.artifacts][drums]")
{
    auto dir = artifactDir().getChildFile ("01_drums");
    dir.createDirectory();

    const int kick = padByName ("Kick");
    const int rim  = padByName ("Rim");
    const int hat  = padByName ("Hat");
    REQUIRE (rim >= 0);

    std::vector<Event> ev;
    auto addHit = [&ev] (int pad, int step, float vel)
    {
        const int note = DrumMachineProcessor::padNote (pad);
        ev.push_back ({ step * kStepSamples, juce::MidiMessage::noteOn (1, note, vel) });
        ev.push_back ({ step * kStepSamples + 200, juce::MidiMessage::noteOff (1, note) });
    };

    for (int s : { 0, 10, 16, 22, 26 }) addHit (kick, s, 1.0f);
    for (int s : { 4, 12, 20, 28 })     addHit (rim,  s, 0.9f);
    for (int s = 0; s < 32; s += 2)     addHit (hat,  s, (s % 4 == 0) ? 0.8f : 0.5f);

    DrumMachineProcessor drums;
    const float peak = renderToWav (drums, ev, 4.5, dir.getChildFile ("c_beat_sidestick.wav"));

    CHECK (peak > 0.1f);
    appendBefund (dir, "c_beat_sidestick.wav - gleicher Groove, Rim statt Snare. Peak "
                       + juce::String (peak, 3)
                       + ". Erwartung: der Sidestick ist ein leiser Akzent, kein Hauptschlag.");
}

TEST_CASE ("Artefakt: Drums - derselbe Beat aus dem Step-Sequencer", "[.artifacts][drums][seq]")
{
    auto dir = artifactDir().getChildFile ("01_drums");
    dir.createDirectory();

    const int kick  = padByName ("Kick");
    const int snare = padByName ("Snare");
    const int hat   = padByName ("Hat");

    DrumMachineProcessor drums;
    drums.clearPattern();
    for (int s : { 0, 10 })       drums.setStep (kick,  s, true);
    for (int s : { 4, 12 })       drums.setStep (snare, s, true);
    for (int s = 0; s < 16; s += 2) drums.setStep (hat, s, true);
    drums.setSeqEnabled (true);

    MovingPlayHead ph;
    drums.setPlayHead (&ph);
    drums.prepareToPlay (kSR, kBlock);

    const double seconds = 4.5;
    const int    total   = (int) std::lround (seconds * kSR);
    juce::AudioBuffer<float> mix (2, total);
    mix.clear();

    float peak = 0.0f;
    for (int start = 0; start < total; start += kBlock)
    {
        const int n = std::min (kBlock, total - start);
        // ppq vor dem Block setzen: Sequencer liest die Position beim Eintritt.
        ph.ppq = (double) start / kSR * (kBpm / 60.0);

        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        juce::MidiBuffer midi;
        drums.processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            mix.copyFrom (ch, start, buf, ch, 0, n);
        peak = std::max (peak, buf.getMagnitude (0, n));
    }
    drums.setPlayHead (nullptr);

    auto out = dir.getChildFile ("b_beat_sequencer.wav");
    out.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto os = out.createOutputStream())
        if (std::unique_ptr<juce::AudioFormatWriter> w (
                fmt.createWriterFor (os.release(), kSR, 2, 24, {}, 0)); w != nullptr)
            w->writeFromAudioSampleBuffer (mix, 0, total);

    CHECK (peak > 0.1f);
    appendBefund (dir, "b_beat_sequencer.wav - GLEICHES Muster, aber vom Step-Sequencer "
                       "transport-synchron getriggert. Peak " + juce::String (peak, 3)
                       + ". Erwartung: klingt wie a_, laeuft nicht auseinander.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pegel-Charakterisierung: wie laut ist EINE Note, und wie summiert sich das?
// Ziel: unterscheiden zwischen "mein Testmuster ist zu heiss" und
// "die Instrumente haben keinen Headroom".
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Artefakt: Pegel-Charakterisierung der Built-in-Instrumente", "[.artifacts][levels]")
{
    auto dir = artifactDir();
    dir.createDirectory();
    juce::StringArray rows;

    auto peakOf = [] (juce::AudioProcessor& p, const std::vector<Event>& ev, double secs)
    {
        p.prepareToPlay (kSR, kBlock);
        const int total = (int) std::lround (secs * kSR);
        float peak = 0.0f;
        for (int start = 0; start < total; start += kBlock)
        {
            const int n = std::min (kBlock, total - start);
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();
            juce::MidiBuffer midi;
            for (const auto& e : ev)
                if (e.sample >= start && e.sample < start + n)
                    midi.addEvent (e.msg, e.sample - start);
            p.processBlock (buf, midi);
            peak = std::max (peak, buf.getMagnitude (0, n));
        }
        return peak;
    };
    auto hit = [] (int note, float vel) {
        return std::vector<Event> { { 0, juce::MidiMessage::noteOn (1, note, vel) } };
    };

    // --- Drums: jedes Pad einzeln (zeigt die Kit-Balance, nicht nur den Kick) ---
    {
        for (int pad = 0; pad < DrumMachineProcessor::kNumPads; ++pad)
        {
            DrumMachineProcessor d;
            const float p = peakOf (d, hit (DrumMachineProcessor::padNote (pad), 1.0f), 1.5);
            rows.add ("Drums  Pad " + juce::String (pad) + " "
                      + DrumMachineProcessor::padName (pad).paddedRight (' ', 12)
                      + " Peak " + juce::String (p, 3));
        }
    }

    // --- Drums: eine Stimme, dann drei gleichzeitig (Kick+Snare+Hat auf 1) ---
    {
        const int kick = padByName ("Kick"), snare = padByName ("Snare"), hat = padByName ("Hat");
        DrumMachineProcessor d1, d2;
        const float single = peakOf (d1, hit (DrumMachineProcessor::padNote (kick), 1.0f), 1.0);

        std::vector<Event> stack;
        for (int p : { kick, snare, hat })
            stack.push_back ({ 0, juce::MidiMessage::noteOn (1, DrumMachineProcessor::padNote (p), 1.0f) });
        const float three = peakOf (d2, stack, 1.0);

        rows.add ("Drums  1 Stimme (Kick, vel 1.0)     Peak " + juce::String (single, 3));
        rows.add ("Drums  3 Stimmen gleichzeitig       Peak " + juce::String (three,  3));
        CHECK (single > 0.0f);
    }

    // --- Synth: eine Note, dann ein Vierklang, pro Preset ---
    {
        SynthProcessor probe;
        for (int i = 0; i < probe.getNumPrograms(); ++i)
        {
            SynthProcessor s1, s4;
            s1.setCurrentProgram (i);
            s4.setCurrentProgram (i);

            const float one = peakOf (s1, hit (60, 0.8f), 1.5);
            std::vector<Event> chord;
            for (int n : { 48, 55, 60, 64 })
                chord.push_back ({ 0, juce::MidiMessage::noteOn (1, n, 0.8f) });
            const float four = peakOf (s4, chord, 1.5);

            rows.add ("Synth  " + probe.getProgramName (i).paddedRight (' ', 12)
                      + " 1 Note Peak " + juce::String (one, 3)
                      + "  |  4 Noten Peak " + juce::String (four, 3));
        }
    }

    dir.getChildFile ("pegel_messung.txt").replaceWithText (rows.joinIntoString ("\n") + "\n");
    for (const auto& r : rows) WARN (r.toStdString());
}

// ─────────────────────────────────────────────────────────────────────────────
// P0b-L Synth-Presets — identische Noten durch jedes Werks-Preset.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE ("Artefakt: Synth - alle Werks-Presets mit identischen Noten", "[.artifacts][synth][preset]")
{
    auto dir = artifactDir().getChildFile ("02_synth");
    dir.createDirectory();

    // Akkord (C-Dur) + kurze Melodie darüber — zeigt Polyphonie UND Hüllkurve.
    std::vector<Event> ev;
    auto note = [&ev] (int n, int startStep, int lenSteps, float vel)
    {
        ev.push_back ({ startStep * kStepSamples, juce::MidiMessage::noteOn (1, n, vel) });
        ev.push_back ({ (startStep + lenSteps) * kStepSamples, juce::MidiMessage::noteOff (1, n) });
    };
    for (int n : { 48, 55, 60, 64 }) note (n, 0, 12, 0.8f);      // Akkord, 3 Beats
    note (72, 13, 3, 0.9f);
    note (74, 16, 3, 0.9f);
    note (76, 19, 6, 0.9f);

    SynthProcessor probe;
    const int numPresets = probe.getNumPrograms();
    REQUIRE (numPresets >= 6);

    juce::StringArray rendered;
    for (int i = 0; i < numPresets; ++i)
    {
        SynthProcessor synth;
        synth.setCurrentProgram (i);
        auto name = synth.getProgramName (i);

        auto file = dir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2)
                                      + "_" + name.replaceCharacter (' ', '_').toLowerCase() + ".wav");
        const float peak = renderToWav (synth, ev, 4.0, file);

        CHECK (peak > 0.01f);                     // jedes Preset muss klingen
        rendered.add (name + " (Peak " + juce::String (peak, 3) + ")");
    }

    appendBefund (dir, "Alle " + juce::String (numPresets) + " Werks-Presets, IDENTISCHE Noten "
                       "(C-Dur-Akkord + Melodie): " + rendered.joinIntoString (", ")
                       + ". Erwartung: jedes Preset klingt deutlich anders, keins verzerrt/still.");
}
