#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"

namespace te = tracktion::engine;

/**
 * ClipComponent — Visual representation of a single clip on the timeline.
 *
 * Renders a coloured rectangle proportional to clip duration with the clip name
 * drawn inside. One ClipComponent is created per clip in the ArrangementView.
 *
 * Double-clicking a MIDI clip opens PianoRollView in a floating DocumentWindow.
 * Drag-to-move may be added in a future plan.
 * Positioned by ArrangementView based on clip start time and pixelsPerSecond zoom.
 */
class ClipComponent : public juce::Component
{
public:
    ClipComponent (te::Clip& clip, EditManager& editManager);

    void paint           (juce::Graphics& g) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    te::Clip& getClip() const noexcept { return clip; }

private:
    te::Clip&    clip;
    EditManager& editManager;

    // Waveform thumbnail — loaded asynchronously from the clip's source file
    // Cache is per-component; for a production app it would be shared, but for Phase 1
    // one cache per component is simpler and avoids shared-state coupling.
    juce::AudioFormatManager        formatManager;
    juce::AudioThumbnailCache       thumbnailCache { 128 };
    juce::AudioThumbnail            thumbnail      { 512, formatManager, thumbnailCache };
    bool                            thumbnailLoaded { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipComponent)
};
