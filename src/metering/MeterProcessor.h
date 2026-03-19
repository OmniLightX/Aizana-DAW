#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <ebur128.h>  // libebur128 C header (MIT)

#include <atomic>
#include <thread>
#include <mutex>

/**
 * MeterProcessor — Real-time Peak / RMS / LUFS metering (EXP-03)
 *
 * Architecture (from RESEARCH.md §LUFS Metering + Pitfall 6):
 *   - Audio thread (processBlock): pushes samples into lock-free FIFO
 *     (juce::AbstractFifo) + computes instantaneous peak into atomic<float>.
 *     NO libebur128 calls on the audio thread.
 *   - Message thread (timerCallback / flushFifo): reads FIFO, feeds
 *     libebur128_add_frames_float(), calls loudness queries, stores results
 *     in atomics for lock-free access from UI.
 *
 * Thread safety:
 *   - getPeakDB() / getRMSDB() / getLUFSMomentary() / getLUFSIntegrated():
 *     safe to call from any thread — read from std::atomic values.
 *   - flushFifo(): must be called from the message thread ONLY.
 *   - prepareToPlay() / releaseResources(): call from message thread before
 *     starting / after stopping the audio engine.
 */
class MeterProcessor : public juce::AudioProcessor
{
public:
    MeterProcessor();
    ~MeterProcessor() override;

    // -----------------------------------------------------------------------
    // Thread-safe getters — read from atomic values updated in flushFifo()
    // -----------------------------------------------------------------------

    /** Instantaneous peak level in dBFS (computed in processBlock on audio thread). */
    float getPeakDB() const;

    /**
     * Atomically read and reset the peak level to -100 dBFS.
     * Use this in timerCallback() so each 24fps window sees only the peak from
     * the current interval — prevents the ratchet-upward-forever bug where audio
     * bursts leave peakDB stuck at the max level indefinitely.
     */
    float readAndResetPeakDB();

    /** RMS level in dBFS (computed over the samples read since the last flush). */
    float getRMSDB() const;

    /** EBU R128 momentary loudness in LUFS (400 ms window). */
    float getLUFSMomentary() const;

    /** EBU R128 integrated loudness in LUFS (gated, full-program measurement). */
    float getLUFSIntegrated() const;

    // -----------------------------------------------------------------------
    // Explicit FIFO flush — call from message thread (or test harness).
    // Reads all pending samples from the FIFO, feeds libebur128, and updates
    // the atomic meters. Replaces the private timerCallback in headless tests.
    // -----------------------------------------------------------------------
    void flushFifo();

    // -----------------------------------------------------------------------
    // Alternative feed path — called from AudioDeviceManager callback (audio
    // thread). Accepts raw pointer arrays from the device output buffer.
    // Bypasses the need to insert into Tracktion Engine's plugin chain.
    //
    // AUDIO THREAD: only updates peakDB and writes to FIFO (lock-free).
    // NEVER calls libebur128 — that remains on the message thread in flushFifo().
    // -----------------------------------------------------------------------
    void pushAudioData (const float* const* channelData,
                        int numChannels,
                        int numSamples);

    // -----------------------------------------------------------------------
    // AudioProcessor interface
    // -----------------------------------------------------------------------

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void releaseResources() override;

    // --- Required stubs (this is a meter, not an effect) ---
    const juce::String getName() const override { return "MeterProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    // Lock-free FIFO — written by audio thread, read by message thread.
    // 65536 frames at 48kHz ≈ 1.37 seconds of headroom.
    static constexpr int kFifoSize = 65536;
    juce::AbstractFifo meterFifo { kFifoSize };
    juce::AudioBuffer<float> meterBuffer;  // backing storage for the FIFO

    int numChannels { 2 };
    double currentSampleRate { 44100.0 };

    // libebur128 state — ONLY touched on the message thread (in flushFifo /
    // prepareToPlay / releaseResources).  The audio thread never calls libebur128.
    ebur128_state* ebuState { nullptr };

    // Atomics — written on message thread (flushFifo), read from any thread.
    std::atomic<float> peakDB { -100.0f };
    std::atomic<float> rmsDB  { -100.0f };
    std::atomic<float> lufsM  { -100.0f };  // momentary
    std::atomic<float> lufsI  { -100.0f };  // integrated

    // Protect ebuState re-initialisation between prepareToPlay and flushFifo.
    // This lock is NOT acquired on the audio thread — only on message thread.
    juce::CriticalSection ebuLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterProcessor)
};
