#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "EditTool.h"

namespace te = tracktion::engine;

class ClipEditor;

/**
 * ClipComponent — Visual representation of a single clip on the timeline.
 *
 * Renders a coloured rectangle proportional to clip duration with the clip name
 * drawn inside. One ClipComponent is created per clip in the ArrangementView.
 *
 * Double-clicking a MIDI clip opens PianoRollView in a floating DocumentWindow.
 * Left-drag repositions the clip along the timeline (undoable via ClipEditor::moveClip).
 * Positioned by ArrangementView based on clip start time and pixelsPerSecond zoom.
 */
class ClipComponent : public juce::Component
{
public:
    ClipComponent (te::Clip& clip, EditManager& editManager, ClipEditor& clipEditor,
                   double& pixelsPerSecond, const EditTool& activeTool);

    void paint            (juce::Graphics& g) override;
    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseMove        (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    juce::MouseCursor getMouseCursor() override;

    te::Clip& getClip() const noexcept { return clip; }

    // Selection state — visual highlight + enables Cmd+C/V in ArrangementView
    bool isSelected() const noexcept { return selected; }
    void setSelected (bool shouldBeSelected);

    /** Fired on left-click (Pointer tool) so ArrangementView can update selectedClips. */
    std::function<void(ClipComponent*)> onSelectRequest;

    /** Fired on Cmd+left-click (Pointer tool) so ArrangementView can ADD this clip to selection. */
    std::function<void(ClipComponent*)> onCmdSelectRequest;

    /** Fired on every mouseDrag.  absoluteYInContainer is the mouse Y relative to
     *  timelineContainer so ArrangementView can determine the target track directly.
     *  The ClipComponent* self pointer lets ArrangementView reparent the component
     *  in-place rather than destroying it mid-drag.
     *  ArrangementView wires this in rebuildTrackRows(). */
    std::function<void(te::Clip&, double startTimeSecs, int absoluteYInContainer,
                       ClipComponent* self)> onInterTrackDrag;

    /** Fired on mouseUp after an inter-track move so ArrangementView can rebuild
     *  track rows once the drag is complete (rather than mid-drag).
     *  Only fires when pendingRebuild has been set by moveClipByTrackDelta. */
    std::function<void()> onDragEnded;

    /** Set by ArrangementView::moveClipByTrackDelta when a cross-track move occurs.
     *  Cleared after onDragEnded fires in mouseUp. */
    bool pendingRebuild = false;

    /** Fired once in mouseDown when drag initialisation begins.
     *  ArrangementView uses this to snapshot initial positions of all selected clips. */
    std::function<void(ClipComponent*)> onDragWillStart;

    /** Fired on every mouseDrag with the snapped time delta from drag origin (seconds).
     *  ArrangementView uses this to move all OTHER selected clips by the same delta. */
    std::function<void(te::Clip&, double deltaSecs)> onMultiClipDrag;

    // ---- Fade handle types ----
    enum class FadeDragType { None, FadeIn, FadeOut, FadeInCurve, FadeOutCurve };

    /** Returns which fade element (if any) is under the given local position. */
    FadeDragType getFadeHandleAt (juce::Point<int> localPos) const;

    /** Draws the fade overlays and corner handles on top of the clip body. */
    void drawFadeOverlay (juce::Graphics& g, float fadeInSecs, float fadeOutSecs);

    /** Returns the hit rect for the clip gain handle (top-center of header band). */
    juce::Rectangle<float> getGainHandleRect() const;

private:
    te::Clip&          clip;
    EditManager&       editManager;
    ClipEditor&        clipEditor;
    double&            pixelsPerSecond;
    const EditTool&    activeTool_;   // reference to ArrangementView::activeTool

    bool selected = false;

    // Drag state
    double dragStartClipTimeSecs = 0.0;  // clip start time when drag began
    int    dragStartMouseX       = 0;    // mouse X in parent coords when drag began
    int    dragStartMouseY       = 0;    // mouse Y in timelineContainer coords when drag began
    bool   isDragging            = false;

    // Fade handle drag state
    FadeDragType activeFadeDrag  = FadeDragType::None;
    float  fadeDragStartValue    = 0.0f; // fade duration (secs) at drag start
    int    fadeDragStartX        = 0;    // mouse X (local) at drag start; repurposed as start-Y for curve drags

    // Continuous fade curve values — range -1.0 (exponential) to 0.0 (linear) to +1.0 (logarithmic).
    // Updated live during a curve-dot drag so drawFadeOverlay can render a smooth bezier.
    // At rest, synced from te::IDs::fadeInType / fadeOutType (0→0.0, 1→+1.0, 2→-1.0).
    mutable float fadeInCurve  = 0.0f;
    mutable float fadeOutCurve = 0.0f;

    // --- Clip Gain handle ---
    // Mutable: synced from TE state in paint() when not dragging
    mutable float currentGainDb { 0.0f };

    // Drag state
    bool  gainDragActive   { false };
    float gainDragStartY   { 0.0f };
    float gainDragStartDb  { 0.0f };

    // --- Non-destructive Clip Trim ---
    enum class TrimSide { None, Left, Right };
    TrimSide activeTrimSide    { TrimSide::None };
    double trimDragStartX      { 0.0 };   // screen X at mouseDown
    double trimDragStartSecs   { 0.0 };   // clip timeline start at mouseDown
    double trimDragStartEnd    { 0.0 };   // clip timeline end at mouseDown
    double trimDragStartOffset { 0.0 };   // source offset at mouseDown
    double trimMaxExtendLeft   { 0.0 };   // earliest possible start (offset floor = 0)
    double trimMaxExtendRight  { 0.0 };   // latest possible end (source file end)

    juce::Rectangle<float> getLeftTrimZone()  const;
    juce::Rectangle<float> getRightTrimZone() const;

    // Click-zone Marquee: set when mouse-down lands in the lower half of this clip
    // while in Pointer mode — forwards drag/up to ArrangementView like a Marquee drag.
    bool inMarqueeZoneDrag = false;

    // Waveform thumbnail — loaded asynchronously from the clip's source file
    // Cache is per-component; for a production app it would be shared, but for Phase 1
    // one cache per component is simpler and avoids shared-state coupling.
    juce::AudioFormatManager        formatManager;
    juce::AudioThumbnailCache       thumbnailCache { 128 };
    juce::AudioThumbnail            thumbnail      { 512, formatManager, thumbnailCache };
    bool                            thumbnailLoaded { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipComponent)
};
