#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace aizana
{
// Message-thread subscription; keeps the track alive through deferred UI teardown.
class TrackMeterSubscription
{
public:
    explicit TrackMeterSubscription (tracktion::engine::AudioTrack& t) : track (&t)
    {
        plugin = track->getLevelMeterPlugin();
        if (plugin != nullptr)
            plugin->measurer.addClient (client);
    }
    ~TrackMeterSubscription() { detach(); }
    void detach()
    {
        // Undo removes the plugin from the track BEFORE deferred UI teardown.
        // Unregister from the exact original object, which the graph may still own.
        if (plugin != nullptr)
            plugin->measurer.removeClient (client);
        plugin = nullptr;
    }
    tracktion::engine::LevelMeasurer::Client client;
private:
    tracktion::engine::AudioTrack::Ptr track;
    juce::ReferenceCountedObjectPtr<tracktion::engine::LevelMeterPlugin> plugin;
    JUCE_DECLARE_NON_COPYABLE (TrackMeterSubscription)
};
}
