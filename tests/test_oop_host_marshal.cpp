// test_oop_host_marshal.cpp — Phase 5, E5.3a (out-of-process plugin hosting, host side).
// Drives OobAudioMarshal (host) against a real child PluginHostBridge over actual shm:
//  - ok path: audio survives the round trip with correct planar channel mapping,
//  - hang/crash path: a missing child yields childGone and the buffer is SILENCED, never
//    leaking stale audio (the bounded-silence hang-immunity that is the point of E5).
// Two threads here; the real process spawn (E5.3b coordinator/worker) is user-tested.

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
    struct ScaleProcessor : juce::AudioProcessor
    {
        const juce::String getName() const override { return "Scale"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.applyGain (c, 0, b.getNumSamples(), (float) (c + 1));
        }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
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
}

TEST_CASE ("OobAudioMarshal round-trips a host buffer through a child plugin", "[oop][marshal]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 512, name);
    REQUIRE (host != nullptr);

    std::atomic<bool> stop { false }, attached { false };
    std::thread child ([&]
    {
        auto c = SharedAudioRing::attachChild (name);
        REQUIRE (c != nullptr);
        ScaleProcessor proc;
        proc.prepareToPlay (44100.0, (int) c->maxBlockSamples());
        attached.store (true);
        aizana::PluginHostBridge::run (*c, proc, [&] { return stop.load(); });
    });

    while (! attached.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    juce::AudioBuffer<float> buffer (2, 64);
    for (int i = 0; i < 64; ++i) { buffer.setSample (0, i, 1.0f); buffer.setSample (1, i, 1.0f); }

    const auto st = aizana::OobAudioMarshal::hostProcessBlock (*host, buffer, 200000 /* 200 ms */);
    REQUIRE (st == SharedAudioRing::Status::ok);

    for (int i = 0; i < 64; ++i)
    {
        REQUIRE (buffer.getSample (0, i) == Catch::Approx (1.0f));   // ch0 × 1
        REQUIRE (buffer.getSample (1, i) == Catch::Approx (2.0f));   // ch1 × 2
    }

    stop.store (true);
    child.join();
}

TEST_CASE ("OobAudioMarshal silences the buffer when no child is hosting", "[oop][marshal]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 256, name);
    REQUIRE (host != nullptr);

    juce::AudioBuffer<float> buffer (2, 64);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i) buffer.setSample (ch, i, 0.9f);   // pre-fill with signal

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto st = aizana::OobAudioMarshal::hostProcessBlock (*host, buffer, 500000);
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;

    REQUIRE (st == SharedAudioRing::Status::childGone);
    REQUIRE (elapsedMs < 100.0);                         // never blocks waiting on a dead child
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE (buffer.getSample (ch, i) == 0.0f);  // silenced, not stale signal
}
