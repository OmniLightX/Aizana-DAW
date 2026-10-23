#include "AizanaEngineBehaviour.h"
#include <algorithm>

namespace te = tracktion::engine;
namespace { const juce::Identifier priorityKey { "aizanaRecordingPriority" }; }

juce::int64 AizanaEngineBehaviour::recordingPriority (const te::Clip& clip)
{
    return (juce::int64) clip.state.getProperty (priorityKey, 0);
}

void AizanaEngineBehaviour::preferRecording (te::Clip& clip)
{
    auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    if (track == nullptr || dynamic_cast<te::WaveAudioClip*> (&clip) == nullptr)
        return;
    juce::int64 next = 1;
    for (auto* other : track->getClips())
        next = std::max (next, recordingPriority (*other) + 1);
    clip.state.setProperty (priorityKey, next, &clip.edit.getUndoManager());
}

void AizanaEngineBehaviour::newClipAdded (te::Clip& clip, bool fromRecording)
{
    if (fromRecording)
        preferRecording (clip);
}

juce::Array<tracktion::TimeRange> AizanaEngineBehaviour::getClipPlaybackMuteTimes (
    const te::Clip& clip, const juce::Array<te::Clip*>* allowedClips)
{
    juce::Array<tracktion::TimeRange> ranges;
    auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    if (track == nullptr || dynamic_cast<const te::WaveAudioClip*> (&clip) == nullptr)
        return ranges;
    for (auto* other : track->getClips())
        if (other != &clip && dynamic_cast<te::WaveAudioClip*> (other) != nullptr
            && ! other->disabled
            && (allowedClips == nullptr || allowedClips->contains (other))
            && recordingPriority (*other) > recordingPriority (clip))
        {
            auto overlap = clip.getPosition().time.getIntersectionWith (other->getPosition().time);
            if (! overlap.isEmpty()) ranges.add (overlap);
        }
    std::sort (ranges.begin(), ranges.end(), [] (auto a, auto b) { return a.getStart() < b.getStart(); });
    juce::Array<tracktion::TimeRange> merged;
    for (auto range : ranges)
        if (! merged.isEmpty() && merged.getLast().getEnd() >= range.getStart())
            merged.getReference (merged.size() - 1) = { merged.getLast().getStart(), std::max (merged.getLast().getEnd(), range.getEnd()) };
        else
            merged.add (range);
    return merged;
}
