#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/ClipEditor.h"
#include "engine/MixerEngine.h"
#include "engine/AutomationEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "TrackHeader.h"
#include "TrackRow.h"
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
 *
 * Plan 01-15: TrackHeaders now receive PluginManager, PluginWindowManager, and
 * PluginScanner so that right-click context menus can insert plugins.
 *
 * Plan 01-18: AutomationEngine is passed in so TrackRows can wire the
 * AutomationLane to it.  Each track row has a disclosure "A" button that
 * expands to reveal the lane below the clip area.
 *
 * Plan 01-25: RulerStrip added above timeline rows; 30ms timer drives playhead
 * redraws; arrow key scrubbing; BPM-reactive ruler repaint.
 */
class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ValueTree::Listener,
                        private juce::ScrollBar::Listener
{
public:
    ArrangementView (ApplicationState&    appState,
                     ClipEditor&          clipEditor,
                     MixerEngine&         mixerEngine,
                     PluginManager&       pluginManager,
                     PluginWindowManager& pluginWindowManager,
                     PluginScanner&       pluginScanner,
                     AutomationEngine&    automationEngine);
    ~ArrangementView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Zoom control (pinch/scroll — also called by MainComponent)
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    void   setPixelsPerSecond (double pps);

    // Add Track button accessor — used by MainComponent for Cmd+T shortcut
    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton; }

    // FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;

    // Keyboard scrubbing — left/right arrows nudge playhead by beat or bar
    bool keyPressed (const juce::KeyPress& key) override;

private:
    // ---- Ruler strip — slim horizontal bar above timeline rows showing bar numbers ----
    class RulerStrip : public juce::Component
    {
    public:
        RulerStrip (ApplicationState& appState, double& pixelsPerSecond, double& scrollPositionSecs);
        void paint (juce::Graphics& g) override;
    private:
        ApplicationState& appState;
        double& pixelsPerSecond;
        double& scrollPositionSecs;
    };

    ApplicationState&    appState;
    ClipEditor&          clipEditor;
    MixerEngine&         mixerEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;
    AutomationEngine&    automationEngine;

    // ---- Header strip ----
    juce::TextButton addTrackButton { "+ Track" };
    bool isDragHovering = false;

    // ---- Ruler strip ----
    RulerStrip rulerStrip;
    static constexpr int kRulerHeight = 18;  // slim ruler, one line of bar number text

    // ---- Playhead ----
    double playheadPositionSecs = 0.0;  // updated by 30ms timer, read by paint()

    // ---- Left panel: track headers ----
    juce::Viewport headerViewport;
    juce::Component headerContainer;   // sized to fit all track headers vertically

    // ---- Right panel: timeline ----
    juce::Viewport timelineViewport;
    juce::Component timelineContainer; // sized to fit all clips

    juce::OwnedArray<TrackHeader> trackHeaders;
    juce::OwnedArray<TrackRow>    trackRows;

    static constexpr int kTrackHeight       = 60;
    static constexpr int kHeaderWidth       = 160;
    static constexpr int kHeaderStripHeight = 28;   // top strip holding Add Track button

    double pixelsPerSecond    = 100.0;
    double scrollPositionSecs =   0.0;

    // Rebuilds all header + TrackRow children from current Edit state
    void rebuildTrackRows();
    // Lays out all TrackRows vertically (called by resized() and after row expansion)
    void layoutTrackRows();

    // Timer::Listener — 30ms playhead position poll
    void timerCallback() override;

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
