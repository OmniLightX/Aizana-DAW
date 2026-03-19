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
 *
 * Plan 01-28: DragAndDropContainer/Target added for track header drag-to-reorder.
 * TrackHeaders call startDragging; ArrangementView receives itemDropped and
 * reorders the track via ValueTree::moveChild (undoable).
 *
 * Plan 01-34: NonScrollingViewport inner class added so left/right arrow keys
 * propagate to ArrangementView::keyPressed for playhead scrubbing instead of
 * being consumed by the Viewport's internal scroll handler.
 *
 * Plan 01-41: ZoomViewport subclass intercepts Ctrl/Cmd+scroll over the timeline
 * and forwards it to ArrangementView::mouseWheelMove — fixing the event-routing
 * gap where the Viewport previously consumed all wheel events. Zoom +/− buttons
 * added to the header strip for discoverability.
 *
 * Plan 01-42: Edit tools toolbar added to the header strip. Three tools:
 * Pointer (default), Range Select (marquee), Scissors (split clip).
 * Keyboard shortcuts E/R/S switch tools. Scissors calls splitClipAtTime
 * which uses duplicate-and-trim (TE v3.2.0 has no public splitAt on clips).
 */

/** Active edit tool — controls mouse behaviour in the timeline area. */
enum class EditTool { Pointer, RangeSelect, Scissors };
class ArrangementView : public juce::Component,
                        public juce::DragAndDropContainer,
                        public juce::DragAndDropTarget,
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

    // DragAndDropTarget overrides — track header drag-to-reorder
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped  (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& details) override;

    // Keyboard scrubbing — left/right arrows nudge playhead by beat or bar
    bool keyPressed (const juce::KeyPress& key) override;

    // Pinch-to-zoom (macOS trackpad) and Cmd+scroll zoom
    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override;

    // Tool-dispatching mouse handlers (Pointer/RangeSelect/Scissors)
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    // Cursor feedback per active tool
    juce::MouseCursor getMouseCursor() override;

private:
    // ---- Ruler strip — slim horizontal bar above timeline rows showing bar numbers ----
    class RulerStrip : public juce::Component
    {
    public:
        RulerStrip (ApplicationState& appState, double& pixelsPerSecond, double& scrollPositionSecs);
        void paint (juce::Graphics& g) override;

        // Click-to-seek: left-click on ruler positions the playhead
        void mouseDown (const juce::MouseEvent& e) override;
        // Drag-to-scrub: dragging along the ruler updates playhead continuously
        void mouseDrag (const juce::MouseEvent& e) override;
        // Signal to users that the ruler is an interactive scrub target
        juce::MouseCursor getMouseCursor() override;

    private:
        ApplicationState& appState;
        double& pixelsPerSecond;
        double& scrollPositionSecs;
    };

    // ---- Viewport subclass that lets left/right arrow keys pass to parent ----
    // Without this, juce::Viewport consumes left/right arrows to scroll its content,
    // blocking ArrangementView::keyPressed from receiving them for playhead scrubbing.
    // Up/down arrows are left to normal Viewport behaviour (vertical scroll).
    struct NonScrollingViewport : public juce::Viewport
    {
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::leftKey
                || key.getKeyCode() == juce::KeyPress::rightKey)
                return false;  // not consumed — propagates up to ArrangementView::keyPressed
            return juce::Viewport::keyPressed (key);
        }
    };

    // ---- Toolbar strip — three edit tool buttons in the header strip ----
    class ToolbarStrip : public juce::Component
    {
    public:
        EditTool activeTool = EditTool::Pointer;
        std::function<void(EditTool)> onToolChange;

        // Unicode symbols: ↖ = Pointer, ⬚ = Range Select, ✂ = Scissors
        juce::TextButton pointerBtn  { u8"\u2196" };
        juce::TextButton selectBtn   { u8"\u2B1A" };
        juce::TextButton scissorsBtn { u8"\u2702" };

        ToolbarStrip();
        void resized() override;
        void paint   (juce::Graphics& g) override;
        void updateButtonStates();
    };

    // ---- Viewport subclass that intercepts Ctrl/Cmd+scroll for zoom ----
    // The timeline viewport sits between the user's mouse and ArrangementView.
    // juce::Viewport normally consumes all wheel events for scrolling, so
    // ArrangementView::mouseWheelMove is never called when the cursor is over
    // the timeline. ZoomViewport intercepts wheel events that carry Ctrl/Cmd and
    // forwards them via onZoomWheel, while passing plain scroll events to the
    // base class (NonScrollingViewport) unchanged.
    struct ZoomViewport : public NonScrollingViewport
    {
        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onZoomWheel;

        void mouseWheelMove (const juce::MouseEvent& e,
                             const juce::MouseWheelDetails& w) override
        {
            if ((e.mods.isCtrlDown() || e.mods.isCommandDown()) && onZoomWheel)
                onZoomWheel (e, w);
            else
                NonScrollingViewport::mouseWheelMove (e, w);
        }
    };

    ApplicationState&    appState;
    ClipEditor&          clipEditor;
    MixerEngine&         mixerEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;
    AutomationEngine&    automationEngine;

    // ---- Header strip ----
    juce::TextButton addTrackButton  { "+ Track" };
    juce::TextButton zoomInButton    { "+" };
    juce::TextButton zoomOutButton   { juce::String::fromUTF8 ("\xe2\x88\x92") };  // Unicode minus sign
    bool isDragHovering = false;

    // ---- Track selection and drag-to-reorder ----
    TrackHeader* selectedTrackHeader = nullptr;
    int          dragOverRowIndex    = -1;  // row index for drop indicator line; -1 = none

    // ---- Marquee selection ----
    bool             isMarqueeActive = false;
    juce::Point<int> marqueeStartPoint;          // in ArrangementView coordinates
    juce::Rectangle<int> marqueeRect;            // current marquee rectangle (screen coords)
    juce::Array<ClipComponent*> selectedClips;   // clips inside the marquee

    void setSelectedTrackHeader (TrackHeader* header);

    // ---- Ruler strip ----
    RulerStrip rulerStrip;
    static constexpr int kRulerHeight = 18;  // slim ruler, one line of bar number text

    // ---- Playhead ----
    double playheadPositionSecs = 0.0;  // updated by 30ms timer, read by paint()

    // ---- Left panel: track headers ----
    NonScrollingViewport headerViewport;
    juce::Component headerContainer;   // sized to fit all track headers vertically

    // ---- Right panel: timeline ----
    ZoomViewport     timelineViewport;
    juce::Component  timelineContainer; // sized to fit all clips

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
