#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/AutomationEngine.h"
#include "ui/automation/AutomationLane.h"

namespace te = tracktion::engine;

/**
 * TrackRow — Expandable row for a single AudioTrack in ArrangementView.
 *
 * Wraps:
 *   - clipArea: a plain juce::Component into which ClipComponents are placed.
 *   - AutomationLane (optional): shown below the clip area when expanded.
 *
 * A small "A" disclosure button in the bottom-left of the clip area toggles
 * the lane.  When collapsed the lane is destroyed; automation data persists
 * in the Edit ValueTree via AutomationEngine.
 *
 * Heights:
 *   - Collapsed: kCollapsedHeight (60 px)
 *   - Expanded:  kExpandedHeight  (60 + 80 = 140 px)
 *
 * Plan 01-18.
 */
class TrackRow : public juce::Component
{
public:
    static constexpr int kCollapsedHeight      = 60;
    static constexpr int kAutomationLaneHeight = 80;
    static constexpr int kExpandedHeight       = kCollapsedHeight + kAutomationLaneHeight;
    static constexpr int kDisclosureButtonSize = 24;

    TrackRow (te::AudioTrack& track,
              AutomationEngine& automationEngine,
              double& pixelsPerSecond,
              double& scrollPositionSeconds);

    bool isExpanded()        const { return expanded; }
    int  getPreferredHeight() const;  // Returns kCollapsedHeight or kExpandedHeight
    void toggleAutomationLane();      // Called by disclosure button

    /** Repositions all ClipComponent children at the given zoom level.
     *  Call this from layoutTrackRows() on every zoom/resize cycle. */
    void layoutClips (double pixelsPerSecond);

    /** Called after expand/collapse so ArrangementView can re-layout headers + timeline. */
    std::function<void()> onExpansionChanged;

    /** Clip area subcomponent — ArrangementView adds ClipComponents here. */
    juce::Component& getClipArea()           { return clipArea; }
    AutomationLane*  getAutomationLane()     { return expanded ? automationLane.get() : nullptr; }

    void mouseDown (const juce::MouseEvent& e) override;
    void resized() override;
    void paint   (juce::Graphics& g) override;

private:
    te::AudioTrack&   track;
    bool              expanded = false;

    juce::Component   clipArea;                         // Clip blocks live here
    std::unique_ptr<AutomationLane> automationLane;

    // "A" disclosure button — toggling expands/collapses the automation lane
    juce::TextButton  disclosureButton { "A" };

    AutomationEngine& automationEngine;
    double&           pixelsPerSecond;
    double&           scrollPositionSeconds;

    void buildDisclosureButton();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackRow)
};
