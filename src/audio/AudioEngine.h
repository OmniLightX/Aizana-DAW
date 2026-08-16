#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include "metering/MeterProcessor.h"
#include "metering/MasterMeterAudioProcessor.h"

namespace te = tracktion::engine;

/**
 * AudioEngine — wraps Tracktion Engine's device manager for audio hardware access.
 *
 * Design constraints (CONTEXT.md / RESEARCH.md):
 *   - macOS: CoreAudio (JUCE native driver)
 *   - Windows: WASAPI only — no ASIO in Phase 1 (Steinberg SDK GPL incompatibility)
 *   - WASAPI latency: expose both shared mode (default) and low-latency IAudioClient3 mode
 *   - Initialisation failures are non-fatal: logged and surfaced for the preferences dialog
 *
 * Audio device ownership: AudioEngine does NOT own its own juce::AudioDeviceManager.
 * Instead it delegates entirely to te::DeviceManager owned by te::Engine. This is
 * the SINGLE audio connection to hardware for this application — having two competing
 * device managers would cause one to fail silently.
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
 * Plan 01-17: AudioEngine inherits AudioIODeviceCallback so it can tap the secondary
 * output buffer for input peak metering.
 *
 * Plan 01-31: Removed standalone juce::AudioDeviceManager. Now holds te::Engine&
 * and delegates all device management to engine.getDeviceManager() (te::DeviceManager).
 * initialise() calls engine.getDeviceManager().initialise() to open CoreAudio/WASAPI.
 *
 * Plan 01-39: Metering is now wired via MasterMeterAudioProcessor registered with
 * te::DeviceManager::setGlobalOutputAudioProcessor(). TE owns the processor lifetime
 * (unique_ptr transfer). AudioEngine keeps a raw observer pointer for prepareToPlay
 * delegation in changeListenerCallback(). The secondary-callback meter tap is removed.
 */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::ChangeListener
{
public:
    explicit AudioEngine (te::Engine& engine);
    ~AudioEngine() override;

    /**
     * Initialise the audio device manager.
     *
     * Calls te::DeviceManager::initialise() which opens CoreAudio (macOS) or
     * WASAPI (Windows). This is the SINGLE call that connects the application
     * to audio hardware — te::Engine's edit graph routes audio through this same
     * device manager when transport.play() is called.
     *
     * On failure, falls back to default device. On complete failure, returns a
     * non-empty error string. The caller should log the error and show it in
     * preferences — do NOT crash.
     *
     * @param numInputChannels   Desired input channel count (default: 32 — covers professional
     *                           multi-channel interfaces like UAD Apollo; JUCE/TE clamp to device max)
     * @param numOutputChannels  Desired output channel count (default: 2)
     * @return Empty string on success; human-readable error description on failure.
     */
    juce::String initialise (int numInputChannels = 32, int numOutputChannels = 2);

    /**
     * Returns TE's underlying juce::AudioDeviceManager for device configuration.
     *
     * TransportBar uses this to read the current sample rate and to change
     * device setup. This is te::Engine's device manager, NOT a standalone one.
     */
    juce::AudioDeviceManager& getDeviceManager()
    {
        // TE's DeviceManager exposes its underlying juce::AudioDeviceManager as a
        // public member named 'deviceManager' (TracktionEngineAudioDeviceManager,
        // which inherits juce::AudioDeviceManager). No getAudioDeviceManager() accessor.
        return engine.getDeviceManager().deviceManager;
    }

    /**
     * Returns the master MeterProcessor that taps the final mixed output.
     * MixerView's timerCallback reads meter values from this processor.
     * All channel strips share the master meter for Phase 1 (EXP-03).
     */
    MeterProcessor& getMasterMeterProcessor() noexcept { return masterMeterProcessor; }

    /** Returns and clears the raw JUCE input peak for a hardware input channel (dBFS). */
    float getAndClearInputPeakDB (int channelIndex) noexcept;

    /**
     * Returns the current output latency in milliseconds, or 0.0 if no device is open.
     *
     * Sum of output buffer latency + driver reported output latency. Use this
     * to display the actual latency in the preferences dialog.
     */
    double getCurrentLatencyMs() const;

    // -----------------------------------------------------------------------
    // AudioIODeviceCallback — audio thread callbacks
    // Registered with engine.getDeviceManager().getAudioDeviceManager() after
    // initialise() so the Tracktion Engine callback runs first and fills the
    // output buffer before we tap it.
    // -----------------------------------------------------------------------

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // -----------------------------------------------------------------------
    // ChangeListener — notified by AudioDeviceManager when the hardware
    // device changes (hot-plug, sample-rate change, buffer-size change).
    // Fired on the message thread; re-prepares all audio processors so they
    // match the new device parameters before the next audio callback fires.
    // -----------------------------------------------------------------------
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

private:
    te::Engine& engine;

    // Master MeterProcessor — lock-free metering backend (FIFO + atomics).
    // Feeds MixerView channel strip MeterBars (all strips share this master meter).
    MeterProcessor masterMeterProcessor;

    // Raw observer to the MasterMeterAudioProcessor registered with TE's device manager.
    // TE owns the object lifetime (unique_ptr transfer in initialise()).
    // Used only to delegate prepareToPlay() in changeListenerCallback() when the device
    // changes after initialise() has already transferred ownership to TE.
    // Set to nullptr until initialise() registers the processor with TE.
    MasterMeterAudioProcessor* masterMeterAudioProcessorPtr { nullptr };

    // masterMeterPrepared is kept for audioDeviceAboutToStart / audioDeviceStopped.
    // It is NOT used to gate meter tapping — MasterMeterAudioProcessor handles that
    // inside TE's render pipeline.
    //
    // Der Klick lebt NICHT mehr hier. Bis 2026-08-16 mischte dieser Callback einen
    // eigenen, FREI LAUFENDEN MetronomeGenerator dazu: er zaehlte Beats ab dem
    // Einschalten, unabhaengig vom Abspielkopf — er klackte im Stillstand, seine
    // Eins lag nicht auf der Eins des Songs, und Tempowechsel (T1.2) kannte er
    // nicht. Ein Vorzaehler ist darauf nicht baubar. Jetzt macht das TEs ClickNode
    // im Render-Graphen (sample-genau, Tempospur-treu, mit Klickbereich fuer den
    // Vorzaehler) — siehe src/engine/CountIn.h.
    std::atomic<bool> masterMeterPrepared { false };

    static constexpr int kMaxMeteredInputChannels = 64;
    std::array<std::atomic<float>, kMaxMeteredInputChannels> rawInputPeaks {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
