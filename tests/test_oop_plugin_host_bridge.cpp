// test_oop_plugin_host_bridge.cpp — Phase 5, E5.2 (out-of-process plugin hosting).
// Drives PluginHostBridge: a real AudioProcessor processes audio handed across the
// SharedAudioRing. A channel-dependent processor (channel c is scaled by c+1) proves the
// planar shm layout maps to AudioBuffer channels correctly, end to end over real shm.
// Host + child run as two threads here; the actual process spawn is wired in E5.3.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include "plugins/oop/SharedAudioRing.h"
#include "plugins/oop/PluginHostBridge.h"
#include "plugins/oop/OutOfProcessPluginInstance.h"
#include "plugins/oop/ConnectionLostCallback.h"

#include <atomic>
#include <thread>
#include <chrono>

namespace
{
    // Minimal concrete AudioProcessor: scales channel c by (c + 1). Channel-dependent so a
    // wrong planar offset (e.g. using numSamples as stride) would corrupt the result.
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

TEST_CASE ("PluginHostBridge processes a real AudioProcessor over the ring (planar mapping)", "[oop][bridge]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 512, name);
    REQUIRE (host != nullptr);

    std::atomic<bool> stop     { false };
    std::atomic<bool> attached { false };

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

    const uint32_t ch = 2, n = 64;
    const uint32_t stride = host->maxBlockSamples();   // planar per-channel stride
    auto* buf = host->audioData();

    for (uint32_t i = 0; i < n; ++i)
    {
        buf[i]          = 1.0f;   // channel 0
        buf[stride + i] = 1.0f;   // channel 1
    }

    const auto status = host->roundTrip (ch, n, 200000 /* 200 ms */);
    REQUIRE (status == SharedAudioRing::Status::ok);

    for (uint32_t i = 0; i < n; ++i)
    {
        REQUIRE (buf[i]          == Catch::Approx (1.0f));   // ch0 × 1
        REQUIRE (buf[stride + i] == Catch::Approx (2.0f));   // ch1 × 2
    }

    stop.store (true);
    child.join();
}

TEST_CASE ("PluginHostBridge handles a partial block (n < maxBlockSamples)", "[oop][bridge]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (1, 256, name);
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

    const uint32_t n = 32;   // far below the 256 capacity
    auto* buf = host->audioData();
    for (uint32_t i = 0; i < n; ++i) buf[i] = 0.5f;

    REQUIRE (host->roundTrip (1, n, 200000) == SharedAudioRing::Status::ok);
    for (uint32_t i = 0; i < n; ++i) REQUIRE (buf[i] == Catch::Approx (0.5f));   // ch0 × 1

    stop.store (true);
    child.join();
}

// JUCE checks fileMightContainThisPluginType even for an explicitly supplied
// hosting-only description. canScanForPlugins=false alone disables scanning.
TEST_CASE ("OOP descriptions reach the format factory through JUCE", "[oop][format-routing]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    struct ProbeFormat : aizana::OutOfProcessPluginFormat
    {
        bool called = false;
        void createPluginInstance (const juce::PluginDescription&, double, int,
                                   PluginCreationCallback callback) override
        {
            called = true;
            callback (nullptr, "probe factory reached");
        }
    };
    juce::AudioPluginFormatManager formats;
    auto* probe = new ProbeFormat();
    formats.addFormat (probe);
    CHECK_FALSE (probe->canScanForPlugins());
    CHECK_FALSE (probe->fileMightContainThisPluginType ({}));
    for (const auto& identifier : { juce::String ("/compat/Effect.vst3"),
                                    juce::String ("/compat/Effect.clap"),
                                    juce::String ("AudioUnit:Effects/Example") })
    {
        CAPTURE (identifier.toStdString());
        juce::PluginDescription desc;
        desc.name = "Compatibility probe";
        desc.fileOrIdentifier = identifier;
        desc.pluginFormatName = probe->getName();
        probe->called = false;
        juce::String error;
        auto instance = formats.createPluginInstance (desc, 48000.0, 512, error);
        CHECK (instance == nullptr);
        CHECK (probe->called);
        CHECK (error == "probe factory reached");
    }
}

TEST_CASE ("Connection loss callback survives concurrent clearing", "[oop][connection-lifetime]")
{
    aizana::ConnectionLostCallback callback;
    juce::WaitableEvent entered, resume;
    auto payload = std::make_shared<int> (37);
    std::weak_ptr<int> weak = payload;
    std::atomic<int> result { 0 };
    callback.set ([owned = payload, &entered, &resume, &result]
    {
        entered.signal();
        if (resume.wait (2000))
            result.store (*owned);
    });
    payload.reset();
    std::thread ipc ([&] { callback.notify(); });
    const bool started = entered.wait (2000);
    callback.set ({});
    const bool retained = ! weak.expired();
    resume.signal();
    ipc.join();
    REQUIRE (started);
    CHECK (retained);
    CHECK (result.load() == 37);
    CHECK (weak.expired());
    callback.notify(); // disconnected: no-op
}
