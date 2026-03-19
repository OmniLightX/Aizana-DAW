#include "AudioEngine.h"

//==============================================================================
AudioEngine::AudioEngine (te::Engine& engine_)
    : engine (engine_)
{
}

AudioEngine::~AudioEngine()
{
    // Unregister the IO callback and change listener before destruction to
    // avoid dangling pointers in the AudioDeviceManager's lists.
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
    juce::String errorString; // te::DeviceManager::initialise() returns void

    auto& adm = engine.getDeviceManager().deviceManager;

    // Register the metering tap callback AFTER TE's device manager initialise() call.
    // TE registers its own graph-rendering callback first (during initialise()),
    // so our callback runs AFTER the graph renders — correct for metering post-render audio.
    adm.addAudioCallback (this);

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
    // Prepare the master MeterProcessor and MetronomeGenerator with the real
    // device parameters. Both prepared flags are set AFTER prepare() completes
    // so the audio thread never sees a partially-prepared state.
    if (device != nullptr)
    {
        const double sr      = device->getCurrentSampleRate();
        const int    bufSize = device->getCurrentBufferSizeSamples();

        // Clear prepared flags first so the audio thread stops using stale state
        // while we re-prepare (device restart / hot-plug scenario).
        masterMeterPrepared.store (false, std::memory_order_release);
        metronomePrepared.store (false, std::memory_order_release);

        masterMeterProcessor.prepareToPlay (sr, bufSize);
        metronome.prepare (sr, bufSize);

        // Set flags only after prepare completes — audio thread is safe to proceed.
        masterMeterPrepared.store (true, std::memory_order_release);
        metronomePrepared.store (true, std::memory_order_release);
    }
}

void AudioEngine::audioDeviceStopped()
{
    // Called on the message thread when the device stops.
    // Clear both prepared flags to block the IO callback from writing to
    // released resources.
    masterMeterPrepared.store (false, std::memory_order_release);
    metronomePrepared.store (false, std::memory_order_release);
    masterMeterProcessor.releaseResources();
}

void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* /*inputChannelData*/,
    int /*numInputChannels*/,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Tracktion Engine has already filled outputChannelData by this point
    // because our callback was registered AFTER the engine's own callback
    // (see AudioEngine::initialise — addAudioCallback called last).
    if (outputChannelData != nullptr && numSamples > 0)
    {
        // Mix metronome click into output buffer (audio thread — atomic params only).
        // Guard with metronomePrepared: blocks processing if the device was just changed
        // and prepare() has not yet completed on the message thread. Without this guard,
        // MetronomeGenerator::processBlock would run with stale sr from the previous
        // device (or the default 44100 Hz before any prepare() fires), causing the
        // phase accumulator to advance at the wrong rate and producing garbled output.
        // Must run before metering tap so the meter captures click energy too.
        if (metronomePrepared.load (std::memory_order_acquire))
            metronome.processBlock (outputChannelData, numOutputChannels, numSamples);

        // Metering tap — READ-ONLY. Captures post-metronome output for MixerView.
        if (masterMeterPrepared.load (std::memory_order_acquire))
            masterMeterProcessor.pushAudioData (outputChannelData, numOutputChannels, numSamples);
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
            // Clear prepared flags so the audio thread pauses using these processors
            // while we re-prepare them for the new device parameters.
            masterMeterPrepared.store (false, std::memory_order_release);
            metronomePrepared.store (false, std::memory_order_release);

            masterMeterProcessor.prepareToPlay (sr, bufSize);
            metronome.prepare (sr, bufSize);

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
