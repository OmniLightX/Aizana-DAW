#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/ClipEditor.h"
#include "engine/MixerEngine.h"
#include "engine/InputBusManager.h"
#include "engine/AutomationEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "TrackHeader.h"
#include "TrackRow.h"
#include "ClipComponent.h"
#include "MarkerLane.h"
#include "TempoLane.h"
#include "engine/TempoEngine.h"
#include "EditTool.h"

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
 *
 * Plan 01-44: ToolbarStrip removed. Right-click context menu replaces the
 * toolbar — Apple-style cleanliness. Any left-click in the arrangement
 * reclaims keyboard focus so E/R/S shortcuts work reliably. Scissors tool
 * now intercepts ClipComponent::mouseDown so click-on-clip splits correctly.
 */

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
                     AutomationEngine&    automationEngine,
                     InputBusManager&     inputBusManager);
    ~ArrangementView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;

    /** Edit-swap support (File>New/Open). detachFromEdit() MUST run while the
        old Edit is still alive — removes the state listener and destroys all
        track/clip-bound children. attachToEdit() re-binds to the new Edit. */
    void detachFromEdit();
    void attachToEdit();

    /** The currently selected track (or nullptr if none) — used as the target for the
        Pro-Assist palette/rail. TrackHeader is fully included above, so this is inline. */
    te::AudioTrack* getSelectedTrack() const noexcept
    {
        return selectedTrackHeader != nullptr ? &selectedTrackHeader->getTrack() : nullptr;
    }

    /** Select the header belonging to the given track (or clear with nullptr).
        Used by MainComponent to mirror the mixer-strip selection into the
        arrangement (user request 2026-06-13). */
    void selectTrack (te::AudioTrack* track)
    {
        if (track == nullptr)
        {
            setSelectedTrackHeader (nullptr);
            return;
        }
        for (auto* h : trackHeaders)
            if (&h->getTrack() == track)
            {
                setSelectedTrackHeader (h);
                return;
            }
    }

    /** Drop a named marker at the current transport position (menu / shortcut). */
    void addMarkerAtPlayhead() { markerLane.addMarkerAtPlayhead(); }

    /** First selected clip (or nullptr) — polled by the InspectorPanel. */
    te::Clip* getPrimarySelectedClip() const noexcept
    {
        return selectedClips.isEmpty() ? nullptr : &selectedClips.getFirst()->getClip();
    }

    /** Number of clips in the current selection. */
    int getNumSelectedClips() const noexcept { return selectedClips.size(); }

    /** True if this clip component is part of the current selection. */
    bool isClipSelected (const ClipComponent* cc) const noexcept
    {
        return selectedClips.contains (const_cast<ClipComponent*> (cc));
    }

    /** Deletes ALL selected clips in one undo step (multi-selection delete). */
    void deleteSelectedClips();

    /** Merges/"verklebt" the selected clips of one track into a single clip
        (Cmd+J, clip context menu) — one undo step. See ClipEditor::mergeClips. */
    void mergeSelectedClips();

    /** Selection-aware delete (Delete/Backspace, also forwarded from MainComponent):
        active time-range > selected clips > selected track. Returns true if it
        deleted something. */
    bool deleteSelection();

    /** Plugin services — used by ClipComponent for the Event-FX menu. */
    PluginManager&       getPluginManager() noexcept       { return pluginManager; }
    PluginWindowManager& getPluginWindowManager() noexcept { return pluginWindowManager; }
    PluginScanner&       getPluginScanner() noexcept       { return pluginScanner; }

    // Zoom control (pinch/scroll — also called by MainComponent)
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    void   setPixelsPerSecond (double pps);

    // Vertical zoom — Cmd+scroll scales track heights (0.5 = compact, 3.0 = tall)
    double getTrackHeightScale() const noexcept { return trackHeightScale; }

    // Add Track button accessor — used by MainComponent for Cmd+T shortcut
    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton; }

    // FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;

    // KI-Setup hook (Phase 3, Slice 4): set by MainComponent. When ≥3 audio files
    // are dropped, the user is offered "Als Stems importieren (KI-Setup)" — this
    // callback receives the files and runs the stem pipeline.
    std::function<void (const juce::Array<juce::File>&)> onStemsDropped;

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
    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Cursor feedback per active tool
    juce::MouseCursor getMouseCursor() override;

    // Public accessors for ClipComponent scissors forwarding
    int    getTimelineViewportX() const { return timelineViewport.getViewPositionX(); }
    void   splitClipAtTime (double timeSecs, int screenY);  // also called by ClipComponent

    // Track header column width — needed by ClipComponent for scissors coordinate conversion
    // UX1: Header-Spalte ist jetzt RESIZABLE (Drag am rechten Rand der Spalte).
    // Bewusst mutable static: eine ArrangementView pro Fenster, und ClipComponent/
    // TrackRow lesen die Breite weiter als ArrangementView::kHeaderWidth.
    static int kHeaderWidth;   // 120..340, persistiert ("arrangeHeaderWidth")
    static constexpr int kHeaderWidthMin = 120, kHeaderWidthMax = 340, kHeaderWidthDefault = 160;

    // Snap helper — public so ClipComponent can call it during drag without needing
    // a separate reference. Returns snapped time; bypasses snap when Alt is held.
    double snapTimeToGrid (double timeSecs, bool altDown = false) const noexcept;

    /** Snap a time DELTA (not an absolute position) to the nearest grid multiple.
     *  Unlike snapTimeToGrid, this preserves a clip's offset from the grid so that
     *  clips at non-grid positions stay there until moved by ≥ half a grid unit.
     *  Bypasses snap when Alt is held. */
    double snapDeltaToGrid (double deltaSecs, bool altDown = false) const noexcept;

    // Inter-track drag — called by ClipComponent's onInterTrackDrag callback.
    // absoluteYInContainer is the mouse Y in timelineContainer coordinates.
    // Moves clip to whichever track the mouse is currently over.
    // draggedComp: the live ClipComponent being dragged — reparented in-place so the
    // drag continues uninterrupted.  Rebuild is deferred to mouseUp via onDragEnded.
    void moveClipByTrackDelta (te::Clip& clip, double startTimeSecs, int absoluteYInContainer,
                               ClipComponent* draggedComp = nullptr);

    // Deselects all clips — called directly by TrackRow on empty-space click.
    void deselectAllClips();

    /** Applies crossfade to the two selected overlapping WaveAudioClips.
     *  If the two selected clips on the same track overlap, sets fade-out on the
     *  earlier clip and fade-in on the later clip equal to the overlap length.
     *  Uses Equal Power curve (type 1).  No-op if no pair overlaps.
     *  Grouped into one undo transaction. */
    void applyCrossfadeToSelectedClips();

    // Called by ClipComponent when mouse-down lands in the click zone (lower half of clip
    // in Pointer mode).  Starts a temporary time-range selection drag — same as Marquee
    // tool behaviour — without permanently switching the active tool.
    void startRangeSelectionFromClickZone (const juce::MouseEvent& e);

private:
    // Shared tail of filesDropped: import dropped audio files as clip(s)/track(s)
    // (single file → target track at drop time; multiple → one track per file).
    void importFilesAsTracks (const juce::Array<juce::File>& audioFiles,
                              int trackIndex, double dropTimeSecs);

    // ---- Ruler strip — slim horizontal bar above timeline rows showing bar numbers ----
    class RulerStrip : public juce::Component
    {
    public:
        RulerStrip (ApplicationState& appState, double& pixelsPerSecond, double& scrollPositionSecs,
                    double& gridResolution, bool& snapEnabled);
        void paint (juce::Graphics& g) override;

        // Click-to-seek: left-click on ruler positions the playhead
        void mouseDown (const juce::MouseEvent& e) override;
        // Drag loop markers or scrub
        void mouseDrag (const juce::MouseEvent& e) override;
        // Release drag target
        void mouseUp   (const juce::MouseEvent& e) override;
        // Signal to users that the ruler is an interactive scrub target
        juce::MouseCursor getMouseCursor() override;

    private:
        ApplicationState& appState;
        double& pixelsPerSecond;
        double& scrollPositionSecs;
        double& gridResolution;
        bool&   snapEnabled;

        // Loop marker positions in seconds
        double loopInSecs  = 1.0;
        double loopOutSecs = 5.0;

        enum class DragTarget { None, LoopIn, LoopOut, Seek };
        DragTarget dragging = DragTarget::None;
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
        std::function<void (double scaleFactor)> onPinchZoom;

        void mouseWheelMove (const juce::MouseEvent& e,
                             const juce::MouseWheelDetails& w) override
        {
            if ((e.mods.isCtrlDown() || e.mods.isCommandDown()) && onZoomWheel)
                onZoomWheel (e, w);
            else
                NonScrollingViewport::mouseWheelMove (e, w);
        }

        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override
        {
            if (onPinchZoom)
                onPinchZoom (static_cast<double> (scaleFactor));
        }
    };

    ApplicationState&    appState;
    ClipEditor&          clipEditor;
    MixerEngine&         mixerEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;
    AutomationEngine&    automationEngine;
    InputBusManager&     inputBusManager;

    // ---- Header strip ----
    juce::TextButton addTrackButton  { "+ Track" };
    juce::TextButton zoomInButton    { "+" };
    juce::TextButton zoomOutButton   { juce::String::fromUTF8 ("\xe2\x88\x92") };  // Unicode minus sign
    bool isDragHovering = false;

    // ---- Active edit tool ----
    EditTool activeTool = EditTool::Pointer;

    // Changes the active tool and refreshes cursor
    void setActiveTool (EditTool tool);

    // ---- Tool selector strip — slim toolbar showing Pointer + Scissors buttons ----
    // Positioned in the header strip between the Add Track button and the zoom buttons.
    // Active tool is highlighted with an accent colour; clicking switches the tool.
    class ToolSelectorStrip : public juce::Component
    {
    public:
        ToolSelectorStrip (EditTool& activeTool, std::function<void(EditTool)> onToolSelected);
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
    private:
        EditTool& activeTool;
        std::function<void(EditTool)> onToolSelected;
    };

    ToolSelectorStrip toolSelector;

    // ---- Track selection and drag-to-reorder ----
    TrackHeader* selectedTrackHeader = nullptr;
    int          dragOverRowIndex    = -1;  // row index for drop indicator line; -1 = none

    // ---- Inter-track drag guard ----
    // Set to true when moveClipByTrackDelta performs a cross-track move.
    // Suppresses ValueTree-triggered rebuildTrackRows() calls that would fire
    // from clip.moveTo() between mouseDrag events and destroy the live component.
    // Reset to false at the start of rebuildTrackRows() (fired from onDragEnded on mouseUp).
    bool isInterTrackDragActive = false;

    // ---- Ghost drag state (inter-track drag visualization — Ghost/Proxy approach) ----
    // During an inter-track drag the clip stays on its source track (no reparenting).
    // A translucent ghost rect is painted in paintOverChildren on the target track.
    // The actual TE move happens once on mouseUp via commitInterTrackDrag().
    bool           ghostDragActive       = false;
    double         ghostStartSecs        = 0.0;    // snapped clip start on target track
    double         ghostDurationSecs     = 0.0;    // clip duration in seconds
    int            ghostSourceTrackIndex = -1;
    int            ghostTargetTrackIndex = -1;
    juce::Colour   ghostClipColour;
    ClipComponent* ghostSourceComp       = nullptr;

    /** Commits the pending inter-track move (called via onInterTrackDragCommit from ClipComponent::mouseUp).
     *  Performs moveClipToTrack + moveClip once with the final ghost position, then sets pendingRebuild. */
    void commitInterTrackDrag();

    // ---- Multi-clip drag: initial positions snapshot ----
    // Populated by onDragWillStart when a drag starts on a selected clip.
    // Indexed in parallel: multiDragClips[i] started at multiDragInitialTimes[i].
    juce::Array<ClipComponent*> multiDragClips;
    juce::Array<double>         multiDragInitialTimes;

    // ---- Pointer rubber-band (Arrow tool) — drag from empty space selects whole clips ----
    // Independent of the Range/Marquee time-range selection.  Shows a blue outline
    // rectangle; any clip partially or fully inside becomes selected on release.
    bool              pointerBandActive = false;
    juce::Point<int>  pointerBandOrigin;        // AV-coordinate drag start
    juce::Rectangle<int> pointerBandRect;       // current band in AV coordinates

    // ---- Temporary Marquee (Command+drag or Click Zone) ----
    // Set to true during the drag gesture; cleared in mouseUp.
    // Does NOT change activeTool — the Pointer tool stays active in the toolbar.
    bool commandDragMarqueeActive = false;

    // ---- Marquee selection (RangeSelect tool — selects whole clips) ----
    bool             isMarqueeActive = false;
    juce::Point<int> marqueeStartPoint;          // in ArrangementView coordinates
    juce::Rectangle<int> marqueeRect;            // current marquee rectangle (screen coords)
    juce::Array<ClipComponent*> selectedClips;   // clips inside the marquee

    // ---- Time-range selection (Marquee tool — track-aware region select) ----
    bool   rangeActive         = false;
    double rangeStartSecs      = 0.0;  // earlier boundary of the selected range
    double rangeEndSecs        = 0.0;  // later boundary
    double rangeDragOriginSecs = 0.0;  // drag start point (so dragging left works correctly)
    // Track range: which rows the drag covers (inclusive indices into trackRows)
    int    rangeTrackStart     = 0;
    int    rangeTrackEnd       = 0;
    int    rangeDragTrackIndex = 0;

    // Delete the content of the active time range (splits clips at boundaries, removes middle)
    void deleteTimeRange();

    // Helper: map Y in timelineContainer coords to track row index (clamped to valid range)
    int yContainerToTrackIndex (int yInContainer) const;

    /** ArrangementView-y → y im Track-Rows-Container, inkl. vertikalem Scroll.
        Einzige Quelle dieser Umrechnung — siehe Kommentar in der Implementierung. */
    int viewYToContainerY (int yInView) const;

    void setSelectedTrackHeader (TrackHeader* header);

    // ---- Marker lane (named song markers) — sits above the ruler ----
    MarkerLane markerLane;
    static constexpr int kMarkerLaneHeight = 18;

    // ---- UX1: Drag-Griff zwischen Header-Spalte und Timeline ----
    class HeaderResizer : public juce::Component
    {
    public:
        explicit HeaderResizer (ArrangementView& o) : owner (o)
        { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xff111111));
            g.fillRect (getLocalBounds().withWidth (1).withX (getWidth() / 2));
        }
        void mouseDown (const juce::MouseEvent&) override { startW = kHeaderWidth; }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            owner.setHeaderWidth (startW + e.getDistanceFromDragStartX());
        }
    private:
        ArrangementView& owner;
        int startW = kHeaderWidthDefault;
    };
    HeaderResizer headerResizer { *this };
    void setHeaderWidth (int newWidth);   // clamp + relayout + persist

    // ---- Tempo lane (T1.2) — Tempo-/Taktartwechsel, zwischen Marker und Ruler ----
    TempoEngine tempoEngine;
    TempoLane   tempoLane;
    static constexpr int kTempoLaneHeight = TempoLane::kLaneHeight;

    // ---- Ruler strip ----
    RulerStrip rulerStrip;
    static constexpr int kRulerHeight = 18;  // slim ruler, one line of bar number text

    // ---- Playhead ----
    double playheadPositionSecs = 0.0;  // updated by 30ms timer, read by paint()
    bool   wasRecordingLastTick = false;
    double recordingOverlayStartSecs = 0.0;

    // ---- Left panel: track headers ----
    NonScrollingViewport headerViewport;
    juce::Component headerContainer;   // sized to fit all track headers vertically

    // ---- Right panel: timeline ----
    ZoomViewport     timelineViewport;
    juce::Component  timelineContainer; // sized to fit all clips

    juce::OwnedArray<TrackHeader> trackHeaders;
    juce::OwnedArray<TrackRow>    trackRows;

    static constexpr int kTrackHeight       = 60;
    // kHeaderWidth is declared in public section for ClipComponent access
    static constexpr int kHeaderStripHeight = 28;   // top strip holding Add Track button

    // Y where the scrollable timeline content begins: below the header strip,
    // the marker lane, and the ruler. Single source of truth for overlay math.
    static constexpr int kTimelineTopY = kHeaderStripHeight + kMarkerLaneHeight + kTempoLaneHeight + kRulerHeight;

    double pixelsPerSecond    = 100.0;
    double scrollPositionSecs =   0.0;
    double trackHeightScale   =   1.0;  // Vertical zoom: 0.5 (compact) to 3.0 (tall)

    // ---- Snap-to-grid ----
    bool            snapEnabled    = true;
    double          gridResolution = 0.5;   // seconds per grid division (1/4 note at 120 BPM)
    juce::TextButton snapButton    { "Snap" };
    juce::ComboBox   gridCombo;             // 1/1, 1/2, 1/4, 1/8, 1/16

    // ---- Autoscroll — playhead-follow toggle ----
    bool             autoScrollEnabled = true;
    juce::TextButton autoScrollButton  { "F" };

    /** Create `count` tracks at the end (Studio One Add Tracks dialog). midi → MIDI
     *  tracks (each seeded with a 4-bar clip); stereo → audio tracks in stereo format. */
    void createTracks (int count, bool midi, bool stereo, juce::String namePrefix);

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
