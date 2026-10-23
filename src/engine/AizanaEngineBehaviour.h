#pragma once
#include <tracktion_engine/tracktion_engine.h>

/** Recording monitoring and non-destructive playback priority for successive takes. */
struct AizanaEngineBehaviour : tracktion::engine::EngineBehaviour
{
    bool muteTrackContentsWhilstRecording() override { return true; }
    void newClipAdded (tracktion::engine::Clip&, bool fromRecording) override;
    juce::Array<tracktion::TimeRange> getClipPlaybackMuteTimes (
        const tracktion::engine::Clip&, const juce::Array<tracktion::engine::Clip*>*) override;

    // Caller owns the undo transaction. Audio files and clip positions are unchanged.
    static void preferRecording (tracktion::engine::Clip&);
    static juce::int64 recordingPriority (const tracktion::engine::Clip&);
};
