#include "AudioRecorder.h"
#include "engine/EditManager.h"
#include "AudioEngine.h"

//==============================================================================
AudioRecorder::AudioRecorder (EditManager& em, AudioEngine& ae)
    : editManager (em), audioEngine (ae)
{
}

//==============================================================================
void AudioRecorder::arm (te::AudioTrack& track)
{
    // Enable record-arm by iterating all input device instances assigned to this track.
    // Tracktion Engine routes audio input → WaveAudioClip through InputDeviceInstance.
    // This mirrors the canonical armTrack() pattern from the TE examples/common/Utilities.h.
    auto& edit = track.edit;
    for (auto* instance : edit.getAllInputDevices())
        if (te::isOnTargetTrack (*instance, track, 0))
            instance->setRecordingEnabled (track.itemID, true);
}

//==============================================================================
void AudioRecorder::disarm (te::AudioTrack& track)
{
    auto& edit = track.edit;
    for (auto* instance : edit.getAllInputDevices())
        if (te::isOnTargetTrack (*instance, track, 0))
            instance->setRecordingEnabled (track.itemID, false);
}

//==============================================================================
void AudioRecorder::startRecording()
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
    {
        jassertfalse;
        return;
    }

    editManager.applyRecordingLocation();

    // Tracktion Engine's transport record() handles:
    //   - Routing audio input → WaveAudioClip
    //   - Writing to a new audio file in the current session media directory
    //   - Wiring the new clip into the armed track's clip list
    //
    // The second parameter (false) means: do not pre-roll before recording.
    edit->getTransport().record (false);
}

//==============================================================================
void AudioRecorder::stopRecording()
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    edit->getTransport().stop (false, false);
}

//==============================================================================
bool AudioRecorder::isRecording() const
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return false;

    return edit->getTransport().isRecording();
}
