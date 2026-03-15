#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/ClipEditor.h"
#include "engine/MixerEngine.h"
#include "TrackHeader.h"
#include "ClipComponent.h"

namespace te = tracktion::engine;

/**
 * ArrangementView — Linear timeline with track rows, track headers, and clip blocks.
 *
 * Layout:
 *   - Left panel (fixed ~160px): vertical list of TrackHeaders
 *   - Right panel: scrollable timeline with ClipComponents
 *
 * Reactive to Edit's ValueTree: rebuilds track rows and clips when tracks are
 * added/removed or clips change. This is the ValueTree-as-source-of-truth pattern
 * per RESEARCH.md Pattern 1.
 *
 * Track row height: 60px (fixed in Phase 1; variable-height rows in Phase 2).
 */
class ArrangementView : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::ScrollBar::Listener
{
public:
    ArrangementView (ApplicationState& appState,
                     ClipEditor& clipEditor,
                     MixerEngine& mixerEngine);
    ~ArrangementView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Zoom control (pinch/scroll — also called by MainComponent)
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    void   setPixelsPerSecond (double pps);

private:
    ApplicationState& appState;
    ClipEditor& clipEditor;
    MixerEngine& mixerEngine;

    // ---- Left panel: track headers ----
    juce::Viewport headerViewport;
    juce::Component headerContainer;   // sized to fit all track headers vertically

    // ---- Right panel: timeline ----
    juce::Viewport timelineViewport;
    juce::Component timelineContainer; // sized to fit all clips

    juce::OwnedArray<TrackHeader>   trackHeaders;
    juce::OwnedArray<ClipComponent> clipComponents;

    static constexpr int kTrackHeight    = 60;
    static constexpr int kHeaderWidth    = 160;

    double pixelsPerSecond    = 100.0;
    double scrollPositionSecs =   0.0;

    // Rebuilds all header + clip children from current Edit state
    void rebuildTrackRows();

    // ValueTree::Listener — called when tracks are added/removed/reordered
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int)   override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeParentChanged   (juce::ValueTree&) override {}

    // ScrollBar::Listener — keeps header/timeline vertical scroll in sync
    void scrollBarMoved (juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Keep header and timeline scroll positions in sync
    void syncScrollPositions();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
