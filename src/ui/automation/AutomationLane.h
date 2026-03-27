#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/AutomationEngine.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * AutomationLane — Draws and edits automation curves for a single parameter
 * on one AudioTrack.
 *
 * - Left-click in empty space: adds a new automation point at (time, value).
 * - Left-drag on an existing point: moves it.
 * - Right-click on an existing point: removes it.
 * - Listens to the track's ValueTree so it repaints after undo/external changes.
 * - A parameter selector dropdown (Volume / Pan) lets the user switch which
 *   parameter is being drawn.
 *
 * Plan 01-18: initial implementation.
 */
class AutomationLane : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    AutomationLane (te::AudioTrack&    track,
                    const juce::String& parameterID,
                    AutomationEngine&   automationEngine,
                    double&             pixelsPerSecond,        // ArrangementView zoom ref
                    double&             scrollPositionSeconds); // ArrangementView scroll ref

    ~AutomationLane() override;

    /** Switch which parameter this lane displays. */
    void setParameterID (const juce::String& newParamID);
    juce::String getParameterID() const { return parameterID; }

    /** Switch between pointer mode (drag existing points) and draw mode (always create). */
    void setDrawMode (bool drawModeActive);
    bool isDrawMode() const noexcept { return drawMode; }

    void paint           (juce::Graphics& g)                  override;
    void resized         ()                                    override;
    void mouseDown       (const juce::MouseEvent& e)           override;
    void mouseDrag       (const juce::MouseEvent& e)           override;
    void mouseUp         (const juce::MouseEvent& e)           override;
    void mouseMove       (const juce::MouseEvent& e)           override;
    void mouseDoubleClick(const juce::MouseEvent& e)           override;

private:
    te::AudioTrack&   track;
    juce::String      parameterID;
    AutomationEngine& automationEngine;
    double&           pixelsPerSecond;
    double&           scrollPositionSeconds;

    // ValueTree listener — repaints when automation data changes (undo, etc.)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded      (juce::ValueTree&, juce::ValueTree&)         override;
    void valueTreeChildRemoved    (juce::ValueTree&, juce::ValueTree&, int)    override;

    // Coordinate helpers
    double xToTime   (float x)     const; // component-x → seconds
    float  timeToX   (double time) const; // seconds → component-x
    float  valueToY  (float value) const; // [0,1] → component-y (bottom=0, top=1)
    float  yToValue  (float y)     const; // component-y → [0,1]

    // Drag state
    int    draggingPointIndex = -1;
    double dragStartTime      = 0.0;
    float  dragStartValue     = 0.0f;

    // Segment drag state (Cmd+click between points)
    bool   isSegmentDragging      = false;
    int    segmentLeftIndex       = -1;   // index of the left bounding point
    int    segmentRightIndex      = -1;   // index of the right bounding point
    float  segmentDragStartY      = 0.0f; // mouse Y at drag start
    float  segmentLeftStartValue  = 0.0f; // left point value at drag start
    float  segmentRightStartValue = 0.0f; // right point value at drag start

    bool drawMode = false;
    juce::TextButton drawModeButton { "P" };   // Pencil toggle in the lane header

    // Parameter selector (Volume / Pan)
    juce::ComboBox paramSelector;

    // dB tooltip label — shown near cursor during point/segment drag (volume param only)
    juce::Label dBLabel;

    // Drawing helpers
    void drawCurve   (juce::Graphics& g) const;
    void drawPoints  (juce::Graphics& g) const;
    void drawDBScale (juce::Graphics& g) const;

    /** Returns the AutomatableParameter for the current parameterID, or nullptr. */
    te::AutomatableParameter* getParam() const;

    /**
     * Find the index of the automation point closest to a mouse position
     * within the given pixel threshold.  Returns -1 if none found.
     */
    int findPointNear (juce::Point<float> mousePos, float thresholdPx = 5.0f) const;

    /** Find the segment (two bounding point indices) at a given position.
     *  Returns true if position is on the line between two points (within threshold). */
    bool findSegmentAtPosition (juce::Point<float> pos, int& leftIdx, int& rightIdx, float thresh = 6.0f) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLane)
};
