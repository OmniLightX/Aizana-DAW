#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"

namespace te = tracktion::engine;

/**
    MarkerLane — slim horizontal strip above the RulerStrip that shows named song
    markers (Intro / Verse / Drop …), Studio One / Logic style.

    Markers are stored natively in Tracktion Engine via te::MarkerManager — they
    live on the Edit's MarkerTrack as MarkerClips and persist + undo for free.
    This component is purely a view + interaction surface over that state; it
    holds no marker state of its own.

    Interaction model:
      • Double-click empty area  → create a marker at that time (auto-named)
      • Single-click a flag      → locate transport to the marker
      • Drag a flag              → move the marker (one undo step per gesture)
      • Right-click a flag       → Rename… / Delete
      • Right-click empty area   → "Marker hier einfügen"

    Time→x mapping is shared with RulerStrip via the same pixelsPerSecond and
    scrollPositionSecs references owned by ArrangementView, so the lane always
    stays pixel-aligned with the ruler and the clips below.
*/
class MarkerLane : public juce::Component
{
public:
    MarkerLane (ApplicationState& appState,
                double& pixelsPerSecond,
                double& scrollPositionSecs);

    void paint (juce::Graphics& g) override;

    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    juce::MouseCursor getMouseCursor() override;

    /** Create a marker at the current transport position (menu / keyboard entry).
     *  Repaints the lane immediately. One undo step. */
    void addMarkerAtPlayhead();

private:
    te::Edit* getEdit() const;

    double xToTime (int x)      const;
    int    timeToX (double sec) const;

    // Full region bounds of a marker [start .. start+length] in local coords.
    juce::Rectangle<int> regionBounds (te::MarkerClip& clip) const;
    // Topmost marker whose region contains x (with a small grab margin), or nullptr.
    // When non-null, onRightEdge reports whether x is in the right-edge resize zone.
    te::MarkerClip* markerAtX (int x, bool& onRightEdge) const;
    // Re-resolves a marker by its stable EditItemID (safe across async callbacks).
    te::MarkerClip* findMarker (te::EditItemID id) const;

    void createMarkerAt (double timeSecs);
    void showMarkerContextMenu (te::EditItemID id);
    void promptRename (te::EditItemID id);
    void deleteMarker (te::EditItemID id);

    ApplicationState& appState;
    double& pixelsPerSecond;
    double& scrollPositionSecs;

    // Drag gesture state. draggingMarker keeps the clip alive during the drag.
    enum class DragMode { None, Move, Resize };
    te::MarkerClip::Ptr draggingMarker;
    DragMode dragMode = DragMode::None;
    bool didDrag = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MarkerLane)
};
