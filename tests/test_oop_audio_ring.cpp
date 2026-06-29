// test_oop_audio_ring.cpp — Phase 5, E5 (out-of-process plugin hosting / hang hardening).
// Validates the SharedAudioRing transport: cross-"endpoint" round-trip integrity over a real
// POSIX shm region, plus the bounded-wait hang/crash immunity that is the whole point of E5.
// Two endpoints run as two THREADS in this process sharing one shm name — the same shm_open /
// mmap / atomic-sequence protocol the real host + child process use. Spawning a true child
// process is exercised in the next slice (E5.2 Main.cpp bridge mode).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>

#include "plugins/oop/SharedAudioRing.h"

#include <atomic>
#include <thread>
#include <chrono>

TEST_CASE ("SharedAudioRing host creates a usable region", "[oop][ring]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (4, 512, name);

    REQUIRE (host != nullptr);
    REQUIRE (name.startsWithChar ('/'));
    REQUIRE (name.length() <= 31);           // macOS shm name cap
    REQUIRE (host->maxChannels() == 4);
    REQUIRE (host->maxBlockSamples() == 512);
    REQUIRE (host->audioData() != nullptr);
}

TEST_CASE ("SharedAudioRing round-trips audio through a child endpoint", "[oop][ring]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 512, name);
    REQUIRE (host != nullptr);

    std::atomic<bool> stop      { false };
    std::atomic<bool> attached  { false };

    // Child endpoint: doubles every sample in place — a stand-in for "the plugin ran".
    std::thread child ([&]
    {
        auto c = SharedAudioRing::attachChild (name);
        REQUIRE (c != nullptr);
        attached.store (true);
        c->runChildLoop (
            [] (float* data, uint32_t ch, uint32_t n)
            {
                for (uint32_t i = 0; i < ch * n; ++i)
                    data[i] *= 2.0f;
            },
            [&] { return stop.load(); });
    });

    // Wait for the child to attach + enter its pump (alive flag set) before publishing.
    while (! attached.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    const uint32_t ch = 2, n = 64;
    auto* buf = host->audioData();
    for (uint32_t i = 0; i < ch * n; ++i)
        buf[i] = (float) (i + 1);

    const auto status = host->roundTrip (ch, n, 200000 /* 200 ms */);

    REQUIRE (status == SharedAudioRing::Status::ok);
    for (uint32_t i = 0; i < ch * n; ++i)
        REQUIRE (buf[i] == Catch::Approx ((float) (i + 1) * 2.0f));

    stop.store (true);
    child.join();
}

TEST_CASE ("SharedAudioRing returns childGone when no child is running", "[oop][ring]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 256, name);
    REQUIRE (host != nullptr);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto status = host->roundTrip (2, 64, 500000 /* 500 ms timeout we must NOT wait out */);
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;

    REQUIRE (status == SharedAudioRing::Status::childGone);
    REQUIRE (elapsedMs < 100.0);   // alive flag is 0 → return immediately, never block
}

TEST_CASE ("SharedAudioRing times out on a hung child without blocking the host", "[oop][ring]")
{
    juce::String name;
    auto host = SharedAudioRing::createHost (2, 256, name);
    REQUIRE (host != nullptr);

    std::atomic<bool> stop     { false };
    std::atomic<bool> attached { false };

    // Hung plugin: the child is alive and accepts the request, but processing never returns
    // in time (sleeps far longer than the host's timeout). The host must bail, not freeze.
    std::thread child ([&]
    {
        auto c = SharedAudioRing::attachChild (name);
        REQUIRE (c != nullptr);
        attached.store (true);
        c->runChildLoop (
            [&] (float*, uint32_t, uint32_t)
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (300));
            },
            [&] { return stop.load(); });
    });

    while (! attached.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto status = host->roundTrip (2, 64, 5000 /* 5 ms */);
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;

    REQUIRE (status == SharedAudioRing::Status::timedOut);
    REQUIRE (elapsedMs < 100.0);   // bounded by the 5 ms timeout, NOT the child's 300 ms sleep

    stop.store (true);
    child.join();
}
