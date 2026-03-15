#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * ClipComponent — Visual representation of a single clip on the timeline.
 *
 * Renders a coloured rectangle proportional to clip duration with the clip name
 * drawn inside. One ClipComponent is created per clip in the ArrangementView.
 *
 * Drag-to-move is wired in plan 01-09 (ClipEditor integration).
 * Positioned by ArrangementView based on clip start time and pixelsPerSecond zoom.
 */
class ClipComponent : public juce::Component
{
public:
    explicit ClipComponent (te::Clip& clip);

    void paint (juce::Graphics& g) override;

    te::Clip& getClip() const noexcept { return clip; }

private:
    te::Clip& clip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipComponent)
};
