#include "AudioEngine.h"

//==============================================================================
AudioEngine::AudioEngine (te::Engine& engine_)
    : engine (engine_)
{
}

AudioEngine::~AudioEngine()
{
    // Close the audio device BEFORE unregistering callbacks.
    // Without an explicit close, CoreAudio (macOS) may retain an open HAL handle
    // that conflicts with the next application launch — the HAL sees a new client
    // trying to open the same device while the previous one hasn't cleanly released
    // it, producing distorted audio on relaunch.
    //
    // closeAudioDevice() synchronously stops the hardware stream. JUCE then calls
    // audioDeviceStopped() on all still-registered callbacks during this close, which
    // is safe because our members (masterMeterProcessor, metronome) are still alive.
    // After closeAudioDevice(), currentAudioDevice is null, so removeAudioCallback
    // will NOT call audioDeviceStopped() a second time (JUCE guards against this).
    engine.getDeviceManager().deviceManager.closeAudioDevice();
    engine.getDeviceManager().deviceManager.removeAudioCallback (this);
    engine.getDeviceManager().deviceManager.removeChangeListener (this);
}

//==============================================================================
juce::String AudioEngine::initialise (int numInputChannels, int numOutputChannels)
{
    // Initialise TE's device manager — this opens CoreAudio (macOS) or WASAPI (Windows).
    // te::DeviceManager is the SINGLE audio connection to hardware for this application.
    // When transport.play() is called on the Edit, Tracktion Engine routes audio through
    // this same device manager — so it MUST be initialised before playback.
    //
    // NOTE: Do NOT call setAudioDeviceSetup() here to override the sample rate.
    // Professional interfaces (UA, RME, Focusrite) negotiate their preferred rate with
    // CoreAudio/WASAPI during device open. Forcing a rate change immediately after open
    // closes and reopens the audio stream while TE's graph render callback is already
    // registered, causing xruns and crackle on every launch. If users need to change
    // the sample rate, expose it through PreferencesDialog using setAudioDeviceSetup()
    // at user request — never automatically on initialise().
    engine.getDeviceManager().initialise (numInputChannels, numOutputChannels);

    // initialise() always schedules an async update via rescanWaveDeviceList().
    // Flush it synchronously now so WaveInputDevice objects exist before
    // createNewEdit() → ensureContextAllocated() iterates waveInputs.
    // Without this, ensureContextAllocated() sees an empty device list and the
    // EditPlaybackContext is built with 0 wave input instances.
    engine.getDeviceManager().dispatchPendingUpdates();

    juce::String errorString; // te::DeviceManager::initialise() returns void

    auto& adm = engine.getDeviceManager().deviceManager;

    // Verify output channels are active after TE initialises the device.
    // On first launch, TE calls initialiseWithDefaultDevices() which enables two
    // output channels. On second+ launch, TE restores saved XML settings via
    // deviceManager.initialise(xml). If that XML is missing the "audioDeviceOutChans"
    // attribute (e.g., from an older save or a save that omitted it), JUCE falls back
    // to a default parse of "" which produces an empty bitmask — the device opens but
    // routes to no physical outputs. The graph renders internally but nothing reaches
    // the speakers.
    //
    // Fix: after TE initialises, check the bitmask. If it is empty, force-enable the
    // requested output channels on whatever device was opened. This mirrors Logic Pro's
    // behaviour of always routing to the best available output on launch.
    {
        auto setup = adm.getAudioDeviceSetup();
        bool needsUpdate = false;

        if (setup.outputChannels.countNumberOfSetBits() == 0 && adm.getCurrentAudioDevice() != nullptr)
        {
            setup.outputChannels.setRange (0, numOutputChannels, true);
            needsUpdate = true;
            juce::Logger::writeToLog ("AudioEngine: activating " + juce::String (numOutputChannels)
                                      + " output channels (restored from empty bitmask)");
        }

        // Same guard for input channels: TE may have saved 0 input channels in its XML
        // (e.g., if the previous session was opened with the old default numInputChannels = 0).
        // Force-enable the requested count so WaveInputDevice objects are created for each
        // physical input — without this, the per-track input selector shows "No inputs".
        if (setup.inputChannels.countNumberOfSetBits() == 0 && adm.getCurrentAudioDevice() != nullptr)
        {
            setup.inputChannels.setRange (0, numInputChannels, true);
            needsUpdate = true;
            juce::Logger::writeToLog ("AudioEngine: activating " + juce::String (numInputChannels)
                                      + " input channels (restored from empty bitmask)");
        }

        if (needsUpdate)
        {
            auto setupErr = adm.setAudioDeviceSetup (setup, true);
            if (setupErr.isNotEmpty())
                juce::Logger::writeToLog ("AudioEngine: channel routing fallback failed: " + setupErr);

            // setAudioDeviceSetup() triggers TE's changeListenerCallback → rescanWaveDeviceList()
            // → triggerAsyncUpdate(). Without this flush, handleAsyncUpdate() (which creates
            // WaveInputDevice objects) fires AFTER editManager.createNewEdit(), leaving TrackHeaders
            // with getNumWaveInDevices() == 0 and no selectable inputs.
            engine.getDeviceManager().dispatchPendingUpdates();
        }
    }

    // Register the secondary AudioIODeviceCallback for MetronomeGenerator mixing.
    // TE registers its own graph-rendering callback during initialise() so ours runs after.
    adm.addAudioCallback (this);

    // Register MasterMeterAudioProcessor with TE's global output pipeline.
    // TE inserts this processor at the very end of its render pipeline — after all tracks
    // are mixed — so processBlock() receives the true mixed output (not the secondary temp
    // buffer which only contains metronome click energy).
    //
    // TE takes unique_ptr ownership; AudioEngine keeps a raw observer for later
    // prepareToPlay() delegation in changeListenerCallback().
    auto meterProc = std::make_unique<MasterMeterAudioProcessor> (masterMeterProcessor);
    masterMeterAudioProcessorPtr = meterProc.get();
    engine.getDeviceManager().setGlobalOutputAudioProcessor (std::move (meterProc));

    // Register for device-change notifications (hot-plug, sample-rate/buffer-size changes).
    // changeListenerCallback() re-prepares MetronomeGenerator and MasterMeterProcessor
    // so they match the new device parameters before the next audio callback fires.
    adm.addChangeListener (this);

    return errorString;
}

//==============================================================================
// AudioDeviceIOCallbackWithContext — audio thread implementation
//==============================================================================

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called on the message thread before the device starts producing audio.
    // Prepare MetronomeGenerator with the real device parameters.
    //
    // NOTE: masterMeterProcessor.prepareToPlay() is NOT called here — TE calls
    // MasterMeterAudioProcessor::prepareToPlay() (which delegates to masterMeterProcessor)
    // as part of its own device-start sequence via the globalOutputAudioProcessor.
    if (device != nullptr)
    {
        const double sr      = device->getCurrentSampleRate();
        const int    bufSize = device->getCurrentBufferSizeSamples();

        // Clear prepared flags so the audio thread pauses while we re-prepare.
        masterMeterPrepared.store (false, std::memory_order_release);
        metronomePrepared.store (false, std::memory_order_release);

        metronome.prepare (sr, bufSize);

        // Set flags only after prepare completes — audio thread is safe to proceed.
        masterMeterPrepared.store (true, std::memory_order_release);
        metronomePrepared.store (true, std::memory_order_release);
    }
}

void AudioEngine::audioDeviceStopped()
{
    // Called on the message thread when the device stops.
    // Clear prepared flags to block the secondary IO callback (metronome mixing).
    // NOTE: masterMeterProcessor.releaseResources() is NOT called here — TE calls
    // MasterMeterAudioProcessor::releaseResources() (which delegates to masterMeterProcessor)
    // as part of its own device-stop sequence via the globalOutputAudioProcessor.
    masterMeterPrepared.store (false, std::memory_order_release);
    metronomePrepared.store (false, std::memory_order_release);
}

void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // ARCHITECTURE NOTE — JUCE multi-callback temp buffer:
    // JUCE's AudioDeviceManager calls the FIRST registered callback (TE's render) with the
    // REAL hardware output buffer. Every subsequent callback — including this one — receives
    // a SHARED TEMP BUFFER, not the real output. After each secondary callback returns, JUCE
    // ADDS the temp buffer content to the real output (dst[j] += src[j]).
    //
    // Critical consequence: the temp buffer is NEVER zeroed between audio callback cycles.
    // If we add to it without clearing first, previous cycle content accumulates — growing
    // louder each buffer until it fills the output with deafening, escalating noise.
    //
    // Fix: ZERO the temp buffer at the start of every secondary callback. We then write only
    // the signals we own (metronome click). JUCE adds our clean content to TE's rendered audio.
    //
    // Metering: MasterMeterAudioProcessor is registered with TE's globalOutputAudioProcessor,
    // so it runs INSIDE TE's render pipeline where the real mixed output buffer is visible.
    // This secondary callback no longer taps the meter — it is metronome-mixing only.
    if (outputChannelData != nullptr && numSamples > 0)
    {
        // Clear our portion of the temp buffer before writing anything.
        // This prevents accumulation from previous callback cycles.
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        // Mix metronome click into the now-zeroed temp buffer.
        // Guard with metronomePrepared: blocks processing if the device was just changed
        // and prepare() has not yet completed on the message thread.
        if (metronomePrepared.load (std::memory_order_acquire))
            metronome.processBlock (outputChannelData, numOutputChannels, numSamples);

        // NOTE: No metering tap here. MasterMeterAudioProcessor handles metering inside
        // TE's render pipeline — it sees the true mixed output including all audio tracks,
        // not just the metronome-only secondary temp buffer.
    }
}

//==============================================================================
// ChangeListener — device-change handler
//==============================================================================

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* /*source*/)
{
    // Called on the message thread when the AudioDeviceManager's device configuration
    // changes: hot-plug of an external interface (UA Apollo, RME Fireface, Focusrite
    // Scarlett), sample-rate change, or buffer-size change.
    //
    // CoreAudio restarts the audio stream on device change. JUCE will call
    // audioDeviceStopped() then audioDeviceAboutToStart() for the new device.
    // However, changeListenerCallback may fire AFTER audioDeviceAboutToStart() has
    // already run (the notification is asynchronous). We defend against this by
    // re-reading the current device and re-preparing if the device is already open,
    // which is safe because prepareToPlay() is idempotent and the prepared flags
    // clear/set around it.
    //
    // This handles the "connect UA interface while app is running" case: the device
    // manager switches to the new hardware device, fires the change notification,
    // and we ensure MetronomeGenerator and MasterMeterProcessor are prepared for
    // the new device's sample rate and buffer size.
    auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (device != nullptr)
    {
        const double sr      = device->getCurrentSampleRate();
        const int    bufSize = device->getCurrentBufferSizeSamples();

        if (sr > 0.0 && bufSize > 0)
        {
            // Clear prepared flags so the secondary IO callback pauses while we re-prepare.
            masterMeterPrepared.store (false, std::memory_order_release);
            metronomePrepared.store (false, std::memory_order_release);

            // Re-prepare MetronomeGenerator for the new device parameters.
            metronome.prepare (sr, bufSize);

            // Re-prepare MasterMeterAudioProcessor if TE has not already done so.
            // TE may call prepareToPlay on globalOutputAudioProcessor independently,
            // but we call it here as a safety net to ensure the metering FIFO matches
            // the new sample rate before MixerView's timer reads results.
            if (masterMeterAudioProcessorPtr != nullptr)
                masterMeterAudioProcessorPtr->prepareToPlay (sr, bufSize);

            masterMeterPrepared.store (true, std::memory_order_release);
            metronomePrepared.store (true, std::memory_order_release);

            juce::Logger::writeToLog ("AudioEngine: device changed — re-prepared at "
                                      + juce::String (sr) + " Hz, buffer "
                                      + juce::String (bufSize) + " samples");
        }
    }
}

//==============================================================================
double AudioEngine::getCurrentLatencyMs() const
{
    auto* device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return 0.0;

    // Output latency reported by the driver (samples) + current buffer size (samples)
    // divided by the sample rate gives the round-trip latency in seconds.
    double sampleRate = device->getCurrentSampleRate();
    if (sampleRate <= 0.0)
        return 0.0;

    int outputLatencySamples = device->getOutputLatencyInSamples();
    int bufferSizeSamples    = device->getCurrentBufferSizeSamples();

    return (static_cast<double> (outputLatencySamples + bufferSizeSamples) / sampleRate) * 1000.0;
}
