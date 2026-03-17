#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "metering/MeterProcessor.h"
#include "MetronomeGenerator.h"

/**
 * AudioEngine — wraps juce::AudioDeviceManager for CoreAudio (macOS) and WASAPI (Windows).
 *
 * Design constraints (CONTEXT.md / RESEARCH.md):
 *   - macOS: CoreAudio (JUCE native driver)
 *   - Windows: WASAPI only — no ASIO in Phase 1 (Steinberg SDK GPL incompatibility)
 *   - WASAPI latency: expose both shared mode (default) and low-latency IAudioClient3 mode
 *   - Initialisation failures are non-fatal: logged and surfaced for the preferences dialog
 *
 * Audio thread discipline (enforced in DSP processors, documented here as design constraint):
 *   - Any code path touching processBlock() MUST use std::atomic<> for parameter communication
 *   - NO locks (std::mutex, juce::CriticalSection) in the audio callback
 *   - NO heap allocations in the audio callback (no new, no std::string, no logging)
 *   - NO file I/O or system calls in the audio callback
 *   These invariants are enforced in Debug builds via juce::AudioThreadGuard in plan 01-06
 *   when the DSP processors are implemented. Document the constraint here so every
 *   future processBlock() author sees it at the point of entry into the audio subsystem.
 *
 * Plan 01-17: AudioEngine now inherits AudioIODeviceCallback so it can
 * tap the output buffer after Tracktion Engine renders each block. This feeds the
 * master MeterProcessor without requiring insertion into the TE plugin chain.
 */
class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    /**
     * Initialise the audio device manager.
     *
     * Attempts to open the preferred audio device. On failure, falls back to
     * default device. On complete failure, returns a non-empty error string.
     *
     * @param numInputChannels   Desired input channel count (default: 2)
     * @param numOutputChannels  Desired output channel count (default: 2)
     * @return Empty string on success; human-readable error description on failure.
     *         The caller should log the error and show it in preferences — do NOT crash.
     */
    juce::String initialise (int numInputChannels = 2, int numOutputChannels = 2);

    /** Returns the underlying AudioDeviceManager for advanced device configuration. */
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    /**
     * Returns the master MeterProcessor that taps the final mixed output.
     * MixerView's timerCallback reads meter values from this processor.
     * All channel strips share the master meter for Phase 1 (EXP-03).
     */
    MeterProcessor& getMasterMeterProcessor() noexcept { return masterMeterProcessor; }

    /**
     * Returns the MetronomeGenerator for enabling/disabling the click track
     * and syncing BPM. Called from TransportBar (message thread, via atomics).
     */
    MetronomeGenerator& getMetronome() noexcept { return metronome; }

    /**
     * Set WASAPI low-latency preference (Windows only).
     *
     * When true (default), JUCE will request the IAudioClient3 low-latency path
     * which fixes the buffer at ~480 samples (~10ms at 48kHz). Requires WaveRT
     * compatible drivers.
     *
     * When false, WASAPI shared mode is used (10-20ms system mixer latency).
     * See RESEARCH.md Pitfall 5 for details.
     *
     * This preference is applied on the NEXT call to initialise() — it does not
     * reconfigure the already-open device.
     */
    void setPreferLowLatencyWASAPI (bool prefer);

    /**
     * Returns the current output latency in milliseconds, or 0.0 if no device is open.
     *
     * Sum of output buffer latency + driver reported output latency. Use this
     * to display the actual latency in the preferences dialog.
     */
    double getCurrentLatencyMs() const;

    // -----------------------------------------------------------------------
    // AudioIODeviceCallback — audio thread callbacks
    // Registered after deviceManager.initialise() so the Tracktion Engine
    // callback runs first and fills the output buffer before we tap it.
    // -----------------------------------------------------------------------

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    juce::AudioDeviceManager deviceManager;
    bool preferLowLatencyWASAPI = true;

    // Master MeterProcessor — taps the final mixed output via the IO callback.
    // Feeds the MixerView channel strip MeterBars (all strips share this meter
    // showing the program-level mix in Phase 1).
    MeterProcessor masterMeterProcessor;
    std::atomic<bool> masterMeterPrepared { false };

    // MetronomeGenerator — synthesizes click audio directly into the output buffer.
    // Parameters (BPM, enabled, beatsPerBar) communicated via std::atomic<> from
    // the message thread. processBlock() called in audioDeviceIOCallbackWithContext.
    MetronomeGenerator metronome;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
