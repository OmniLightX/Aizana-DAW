#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

/**
 * SharedAudioRing — lock-free, single-block audio transport across a process boundary
 * (E5, out-of-process plugin hosting / plugin-hang hardening).
 *
 * WHY shared memory + atomics, not Apple XPC:
 *   A DAW audio graph must hand one buffer to the plugin and get it back within a single
 *   audio callback (≈1.45 ms at 44.1 kHz / 64 samples) with NO allocation, lock, or I/O on
 *   the audio thread. Apple XPC is message/libdispatch-oriented — wrong tool for a per-buffer
 *   real-time round trip. Every pro plugin bridge (Bitwig, Reaper, …) uses shared memory plus
 *   a sync primitive. We use a POSIX shm region holding two std::atomic<uint32_t> sequence
 *   counters; the host bumps a request counter, the child mirrors it back. No named semaphore:
 *   macOS does NOT implement sem_timedwait, so a bounded wait via timed atomic spin is the only
 *   portable way to get the hang immunity that is the whole point of E5.
 *
 * HANG / CRASH IMMUNITY:
 *   roundTrip() waits at most `timeoutMicros` for the child. If the child hangs OR crashes
 *   (its process is killed, so it cannot even flip the alive flag), the wait expires and the
 *   host gets Status::timedOut / childGone. The host audio thread is NEVER blocked longer than
 *   the timeout — the DAW survives a broken plugin (Studio One behaviour, runtime edition).
 *
 * THREADING:
 *   - roundTrip()  : HOST audio thread. memcpy into a pre-mapped region + atomic spin. No alloc.
 *   - runChildLoop(): CHILD process. Pumps requests, runs the plugin in place, mirrors the seq.
 *   - create/attach/destroy: any non-audio thread.
 *
 * AUDIO LAYOUT (in audioData()): planar, [ch0: maxBlockSamples floats][ch1: …] …
 *   Processing is IN PLACE — the child overwrites the input region with the plugin's output.
 */
class SharedAudioRing
{
public:
    enum class Status
    {
        ok,          // child processed the block; output is in audioData()
        timedOut,    // child did not respond within timeoutMicros (hung or crashed)
        childGone    // child flagged a clean exit; treat like a dropped plugin
    };

    /** HOST side: create + own a fresh shm region sized for maxChannels × maxBlockSamples.
     *  On success, `outName` receives the shm name the child must attach() to. */
    static std::unique_ptr<SharedAudioRing> createHost (uint32_t maxChannels,
                                                        uint32_t maxBlockSamples,
                                                        juce::String& outName);

    /** CHILD side: attach to an existing shm region by the name createHost() produced. */
    static std::unique_ptr<SharedAudioRing> attachChild (const juce::String& name);

    ~SharedAudioRing();

    /** Planar audio scratch region (host writes input here pre-roundTrip, reads output after;
     *  child reads input / writes output here). Capacity = maxChannels × maxBlockSamples. */
    float*   audioData() noexcept { return audio; }
    uint32_t maxChannels()     const noexcept { return capChannels; }
    uint32_t maxBlockSamples() const noexcept { return capSamples;  }
    const juce::String& name() const noexcept { return shmName; }

    // ---- HOST ----------------------------------------------------------------
    /** HOST audio thread. Input must already be in audioData(). Publishes the request, waits
     *  up to timeoutMicros for the child to mirror the sequence, then leaves output in
     *  audioData(). Never allocates / locks. numChannels/numSamples must be ≤ the capacities. */
    Status roundTrip (uint32_t numChannels, uint32_t numSamples, uint32_t timeoutMicros) noexcept;

    // ---- CHILD ---------------------------------------------------------------
    /** CHILD process. Pumps requests until shouldExit() returns true. For each request,
     *  `process(data, numChannels, numSamples)` runs the plugin in place over audioData().
     *  Adaptive backoff keeps a stopped transport from pegging a core. */
    void runChildLoop (const std::function<void (float* data, uint32_t numChannels, uint32_t numSamples)>& process,
                       const std::function<bool()>& shouldExit);

    /** CHILD: mark a clean exit so the host's next roundTrip returns childGone immediately
     *  instead of waiting out the timeout. (Crashes can't call this — timeout covers those.) */
    void markChildExiting() noexcept;

private:
    SharedAudioRing() = default;

    struct Header;   // POD placed at the start of the mapped region

    Header*      hdr        { nullptr };
    float*       audio      { nullptr };   // = mapped base + audioOffset
    void*        mapped     { nullptr };
    size_t       mappedSize { 0 };
    int          fd         { -1 };
    bool         isHost     { false };
    uint32_t     capChannels { 0 };
    uint32_t     capSamples  { 0 };
    juce::String shmName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SharedAudioRing)
};
