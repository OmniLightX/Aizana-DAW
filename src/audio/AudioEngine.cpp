#include "AudioEngine.h"

//==============================================================================
AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    // Unregister the IO callback before destruction to avoid dangling pointer
    // in the AudioDeviceManager's callback list.
    deviceManager.removeAudioCallback (this);
}

//==============================================================================
juce::String AudioEngine::initialise (int numInputChannels, int numOutputChannels)
{
    // Attempt to initialise AudioDeviceManager with the preferred number of channels.
    //
    // Parameters:
    //   numInputs     — desired input channels (0 = no input / output-only)
    //   numOutputs    — desired output channels
    //   savedState    — nullptr on first launch; restored from user preferences in plan 01-08
    //   selectDefaultDeviceOnFailure — true: fall back to system default on failure
    //
    // On macOS: JUCE selects CoreAudio automatically (no further configuration needed).
    // On Windows: JUCE selects WASAPI. The preferLowLatencyWASAPI flag configures
    //   whether to request IAudioClient3 (low-latency exclusive WASAPI) or shared mode.
    //   See AudioDeviceSetup::bufferSize — JUCE uses IAudioClient3 if the buffer size
    //   matches the device's minimum period. This is surfaced in plan 01-08 preferences UI.
    auto errorString = deviceManager.initialise (numInputChannels,
                                                 numOutputChannels,
                                                 nullptr, // no saved state on first launch
                                                 true);   // fall back to default device

    // Note: preferLowLatencyWASAPI affects the next device setup via AudioDeviceSetup.
    // The WASAPI IAudioClient3 path is engaged when the user selects a small buffer size
    // in preferences and JUCE negotiates it with the driver. Phase 1 documents the intent;
    // plan 01-08 (Preferences UI) wires up the buffer size selector.

    // Register the metering tap callback AFTER initialise() so the Tracktion Engine
    // callback (registered during its own device connection) runs first and fills the
    // output buffer before we read it for metering. This guarantees we tap post-render audio.
    deviceManager.addAudioCallback (this);

    return errorString;
}

//==============================================================================
void AudioEngine::setPreferLowLatencyWASAPI (bool prefer)
{
    preferLowLatencyWASAPI = prefer;

    // The WASAPI mode is applied on the next device open via AudioDeviceSetup.
    // If a device is currently open, the change takes effect after the user
    // applies preferences (which calls deviceManager.setAudioDeviceSetup() in plan 01-08).
}

//==============================================================================
// AudioDeviceIOCallbackWithContext — audio thread implementation
//==============================================================================

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // Called on the message thread before the device starts producing audio.
    // Prepare the master MeterProcessor with the real device parameters.
    if (device != nullptr)
    {
        const double sr      = device->getCurrentSampleRate();
        const int    bufSize = device->getCurrentBufferSizeSamples();
        masterMeterProcessor.prepareToPlay (sr, bufSize);
        masterMeterPrepared.store (true, std::memory_order_release);
        metronome.prepare (sr, bufSize);
    }
}

void AudioEngine::audioDeviceStopped()
{
    // Called on the message thread when the device stops.
    // Prevent the IO callback from writing to a released MeterProcessor.
    masterMeterPrepared.store (false, std::memory_order_release);
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
        // Must run before metering tap so the meter captures click energy too.
        metronome.processBlock (outputChannelData, numOutputChannels, numSamples);

        // Metering tap — READ-ONLY. Captures post-metronome output for MixerView.
        if (masterMeterPrepared.load (std::memory_order_acquire))
            masterMeterProcessor.pushAudioData (outputChannelData, numOutputChannels, numSamples);
    }
}

//==============================================================================
double AudioEngine::getCurrentLatencyMs() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
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
