#include "AudioEngine.h"

//==============================================================================
AudioEngine::AudioEngine (te::Engine& engine_)
    : engine (engine_)
{
}

AudioEngine::~AudioEngine()
{
    // Unregister the IO callback before destruction to avoid dangling pointer
    // in the AudioDeviceManager's callback list.
    engine.getDeviceManager().deviceManager.removeAudioCallback (this);
}

//==============================================================================
juce::String AudioEngine::initialise (int numInputChannels, int numOutputChannels)
{
    // Initialise TE's device manager — this opens CoreAudio (macOS) or WASAPI (Windows).
    // te::DeviceManager is the SINGLE audio connection to hardware for this application.
    // When transport.play() is called on the Edit, Tracktion Engine routes audio through
    // this same device manager — so it MUST be initialised before playback.
    engine.getDeviceManager().initialise (numInputChannels, numOutputChannels);
    juce::String errorString; // te::DeviceManager::initialise() returns void

    // --- Sample rate enforcement ---
    // The OS may have the audio device set to a non-standard rate (e.g., 24 kHz from
    // macOS Audio MIDI Setup). A DAW must default to 44.1 kHz or 48 kHz. Correct it here
    // immediately after the device opens so the TransportBar shows a valid rate on launch.
    {
        auto& adm = engine.getDeviceManager().deviceManager;
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        adm.getAudioDeviceSetup (setup);

        const double standardRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        bool isStandard = false;
        for (auto r : standardRates)
            if (std::abs (setup.sampleRate - r) < 1.0)
                { isStandard = true; break; }

        if (! isStandard || setup.sampleRate <= 0.0)
        {
            setup.sampleRate = 44100.0;
            // keepCurrentlyActiveBits = true: preserve active channels, only change rate
            auto srErr = adm.setAudioDeviceSetup (setup, true);
            if (srErr.isNotEmpty())
                juce::Logger::writeToLog ("Sample rate correction warning: " + srErr);
        }
    }
    // --- End sample rate enforcement ---

    // Register the metering tap callback AFTER TE's device manager initialise() call.
    // TE registers its own graph-rendering callback first (during initialise()),
    // so our callback runs AFTER the graph renders — correct for metering post-render audio.
    engine.getDeviceManager().deviceManager.addAudioCallback (this);

    return errorString;
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
