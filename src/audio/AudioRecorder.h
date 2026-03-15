#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_core/juce_core.h>

namespace te = tracktion::engine;

class EditManager;
class AudioEngine;

/**
 * AudioRecorder — Records audio from AudioDeviceManager into a WaveAudioClip
 * via Tracktion Engine's built-in record mode.
 *
 * THREAD CONSTRAINT: All methods must be called from the message thread only.
 *
 * Recording workflow:
 *   1. Call arm(track) — enables the record-arm indicator on the track.
 *   2. Call startRecording() — starts the Edit transport in record mode.
 *      Tracktion Engine handles the audio → WaveAudioClip pipeline automatically.
 *   3. Call stopRecording() — stops transport and finalises the WaveAudioClip.
 *   4. Call disarm(track) — removes record-arm indicator.
 *
 * Hardware requirement: Requires an active audio device open via AudioEngine.
 * Tests that do not require audio hardware should use Renderer (see test_multitrack_render.cpp).
 *
 * Note: For offline/headless render tests, use te::Renderer::renderToFile() directly.
 * This class is for live record-to-disk from audio hardware.
 */
class AudioRecorder
{
public:
    /**
     * @param editManager   Provides access to the Edit and UndoManager.
     * @param audioEngine   Required to access the AudioDeviceManager.
     */
    explicit AudioRecorder (EditManager& editManager, AudioEngine& audioEngine);

    /**
     * Enable the record-arm indicator on a track.
     * The track will capture audio on the next startRecording() call.
     */
    void arm (te::AudioTrack& track);

    /**
     * Disable the record-arm indicator on a track.
     */
    void disarm (te::AudioTrack& track);

    /**
     * Start the Edit transport in record mode.
     * Tracktion Engine automatically routes armed track audio → WaveAudioClip.
     */
    void startRecording();

    /**
     * Stop recording. Stops the transport and finalises the WaveAudioClip.
     */
    void stopRecording();

    /** Returns true if the transport is currently recording. */
    bool isRecording() const;

private:
    EditManager& editManager;
    AudioEngine& audioEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRecorder)
};
