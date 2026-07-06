// test_oop_param_midi.cpp — Phase 5, E5.4 (parameters + MIDI across the OOP boundary).
// Host + child as two threads over REAL shm (same pattern as [oop][bridge]):
//   * writeParamEvents → child applies them to the plugin's parameters before the block
//   * MIDI in → child plugin sees it; plugin MIDI out → host reads it back
//   * region hygiene: a block WITHOUT fresh host writes must not re-apply stale events
// The cross-process version of all of this runs in the oop_host_smoke CTest.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/oop/SharedAudioRing.h"
#include "plugins/oop/PluginHostBridge.h"
#include "plugins/oop/OobAudioMarshal.h"

#include <atomic>
#include <thread>
#include <chrono>

namespace
{
    // Gain parameter + MIDI echo + counts how many times the param was SET (stale-replay guard).
    struct ParamMidiProcessor : juce::AudioProcessor
    {
        juce::AudioParameterFloat* gain = nullptr;
        std::atomic<int> paramSets { 0 };

        struct CountingParam : juce::AudioParameterFloat
        {
            std::atomic<int>& counter;
            CountingParam (std::atomic<int>& c)
                : juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 1.0f),
                  counter (c) {}
            void valueChanged (float) override { ++counter; }
        };

        ParamMidiProcessor()
        {
            addParameter (gain = new CountingParam (paramSets));
        }

        const juce::String getName() const override { return "ParamMidi"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            b.applyGain (gain->get());   // MIDI left untouched = echo back to the host
        }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    struct RingChildFixture
    {
        std::unique_ptr<SharedAudioRing> host;
        ParamMidiProcessor proc;   // lives in the "child" thread's bridge
        std::thread child;
        std::atomic<bool> stop { false }, attached { false };
        juce::String name;

        RingChildFixture (uint32_t channels, uint32_t maxBlock)
        {
            host = SharedAudioRing::createHost (channels, maxBlock, name);
            REQUIRE (host != nullptr);

            child = std::thread ([this]
            {
                auto c = SharedAudioRing::attachChild (name);
                REQUIRE (c != nullptr);
                proc.prepareToPlay (44100.0, (int) c->maxBlockSamples());
                attached.store (true);
                aizana::PluginHostBridge::run (*c, proc, [this] { return stop.load(); });
            });

            while (! attached.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }

        ~RingChildFixture()
        {
            stop.store (true);
            child.join();
        }
    };
}

TEST_CASE ("E5.4: param events cross the ring and drive the child plugin", "[oop][e54]")
{
    RingChildFixture fx (1, 128);

    juce::AudioBuffer<float> buffer (1, 64);
    juce::MidiBuffer midi;

    // Block 1 — no param events: default gain 1.0.
    for (int i = 0; i < 64; ++i) buffer.setSample (0, i, 0.5f);
    aizana::OobAudioMarshal::writeParamEvents (*fx.host, nullptr, 0);
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, midi, 200000)
             == SharedAudioRing::Status::ok);
    REQUIRE (buffer.getSample (0, 10) == Catch::Approx (0.5f));

    // Block 2 — param event gain=0.25 applied BEFORE the block.
    const aizana::OopParamEvent ev { 0, 0.25f };
    aizana::OobAudioMarshal::writeParamEvents (*fx.host, &ev, 1);
    for (int i = 0; i < 64; ++i) buffer.setSample (0, i, 0.5f);
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, midi, 200000)
             == SharedAudioRing::Status::ok);
    REQUIRE (buffer.getSample (0, 10) == Catch::Approx (0.125f));   // 0.5 × 0.25

    const int setsAfterBlock2 = fx.proc.paramSets.load();

    // Block 3 — NO fresh write: the child must not re-apply the stale event (count zeroed).
    aizana::OobAudioMarshal::writeParamEvents (*fx.host, nullptr, 0);
    for (int i = 0; i < 64; ++i) buffer.setSample (0, i, 0.5f);
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, midi, 200000)
             == SharedAudioRing::Status::ok);
    REQUIRE (buffer.getSample (0, 10) == Catch::Approx (0.125f));   // gain stays 0.25
    REQUIRE (fx.proc.paramSets.load() == setsAfterBlock2);
}

TEST_CASE ("E5.4: MIDI rides the ring both ways (echo child)", "[oop][e54]")
{
    RingChildFixture fx (1, 128);

    juce::AudioBuffer<float> buffer (1, 64);
    buffer.clear();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn  (1, 60, (juce::uint8) 100), 3);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60),                    40);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 7, 90),         12);

    aizana::OobAudioMarshal::writeParamEvents (*fx.host, nullptr, 0);
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, midi, 200000)
             == SharedAudioRing::Status::ok);

    // All three events echoed back, sample positions intact.
    int noteOns = 0, noteOffs = 0, ccs = 0, total = 0;
    for (const auto meta : midi)
    {
        ++total;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()  && msg.getNoteNumber() == 60 && meta.samplePosition == 3)  ++noteOns;
        if (msg.isNoteOff() && msg.getNoteNumber() == 60 && meta.samplePosition == 40) ++noteOffs;
        if (msg.isController() && msg.getControllerValue() == 90 && meta.samplePosition == 12) ++ccs;
    }
    REQUIRE (total == 3);
    REQUIRE (noteOns == 1);
    REQUIRE (noteOffs == 1);
    REQUIRE (ccs == 1);

    // Next block with NO input MIDI: nothing may replay from stale regions.
    juce::MidiBuffer empty;
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, empty, 200000)
             == SharedAudioRing::Status::ok);
    REQUIRE (empty.getNumEvents() == 0);
}

TEST_CASE ("E5.4: MIDI region overflow drops the tail, never corrupts", "[oop][e54]")
{
    RingChildFixture fx (1, 128);

    juce::AudioBuffer<float> buffer (1, 64);
    buffer.clear();

    // Far more events than kMidiRegionBytes can hold (each ~12 bytes on the wire).
    juce::MidiBuffer midi;
    const int tooMany = (int) (SharedAudioRing::kMidiRegionBytes / 4);
    for (int i = 0; i < tooMany; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, 1 + (i % 100), (juce::uint8) 100), i % 64);

    aizana::OobAudioMarshal::writeParamEvents (*fx.host, nullptr, 0);
    REQUIRE (aizana::OobAudioMarshal::hostProcessBlock (*fx.host, buffer, midi, 200000)
             == SharedAudioRing::Status::ok);

    // Some events made it (region-limited), and the decode never walked past the region.
    REQUIRE (midi.getNumEvents() > 0);
    REQUIRE (midi.getNumEvents() < tooMany);
}
