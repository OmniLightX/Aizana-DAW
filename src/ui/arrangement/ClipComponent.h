#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "ui/common/AudioThumbnailPool.h"
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
class ClipComponent : public juce::Component,
                      public juce::ChangeListener,
                      public juce::ValueTree::Listener,
                      private te::SelectableListener
{
public:
    ClipComponent (te::Clip& clip, EditManager& editManager, ClipEditor& clipEditor,
                   double& pixelsPerSecond, const EditTool& activeTool);
    ~ClipComponent() override;

    void paint            (juce::Graphics& g) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    // ValueTree::Listener — reloads thumbnail if source property is set after construction
    // (TE's recording pipeline sets IDs::source on the newly-added clip's ValueTree state)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded      (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved    (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged   (juce::ValueTree&) override {}
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

    /** Set to true by ArrangementView when this clip is being ghost-dragged to another track.
     *  The clip dims itself to indicate it will move on mouseUp. */
    bool isGhostDimmed = false;

    /** Fired on mouseUp when an inter-track ghost drag completed (isGhostDimmed was true).
     *  ArrangementView responds by committing the actual TE move and setting pendingRebuild. */
    std::function<void()> onInterTrackDragCommit;

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
    void refreshThumbnail();
    void selectableObjectChanged (te::Selectable*) override { refreshThumbnail(); repaint(); }
    void selectableObjectAboutToBeDeleted (te::Selectable*) override {}
    /** Context-menu action: estimate BPM from the clip's source audio on a
        background thread, then offer to set the song tempo (undoable). */
    void detectTempoFromClip();
    void extractTempoMapFromClip();   // P7a: Grid folgt der Performance
    void applyTempoMap (const juce::Array<double>& beatTimesFileSecs);
    void showVocalTuningDialog();     // P8: Key/Skala/Stärke → VocalTuner (1 Undo)

    /** Context-menu action: estimate the clip's source BPM, then timestretch the
        clip to follow the song tempo (loopInfo BPM + setAutoTempo, undoable). */
    void conformClipToSongTempo();

    /** Context-menu action: stop the clip following the song tempo and restore
        its natural playback length (undoable). */
    void removeSongTempoConform();

    te::Clip&          clip;
    // Keeps the clip object alive for this component's lifetime. ClipComponents are
    // rebuilt asynchronously (callAsync rebuildTrackRows), but a deferred resized()
    // → TrackRow::layoutClips can run FIRST and call clip.getPosition() after the clip
    // was removed from the edit. A reference-counted Ptr keeps the (now detached) Clip
    // valid until this component is destroyed on rebuild — preventing a dangling-clip
    // pointer-auth crash. Declared right after `clip` so both bind the same object.
    te::Clip::Ptr      clipRef;
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

    // Waveform thumbnail — loaded asynchronously from the clip's source file.
    // ONE shared cache/format manager for the whole app (AudioThumbnailPool):
    // ClipComponents are recreated on every rebuildTrackRows, and a per-component
    // cache restarted every waveform read from zero — with 20+ tracks the reads
    // never finished between rebuilds and clips stayed solid-colour.
    // Das Thumbnail gehoert dem POOL, nicht dieser Component: ClipComponents werden
    // bei jedem rebuildTrackRows neu gebaut (beim Import einer Datei gemessen 5x),
    // und ein component-eigenes Thumbnail begann jedes Mal bei null zu lesen — bei
    // langen Dateien wurde es deshalb nie fertig und der Clip blieb einfarbig.
    // Der Zeiger ist nullptr, solange keine aufloesbare Quelldatei existiert.
    juce::SharedResourcePointer<AudioThumbnailPool> thumbnailPool;
    juce::AudioThumbnail*           thumbnail       { nullptr };
    juce::Rectangle<int>            takesBadgeHit;   ///< Trefferflaeche des Takes-Badges (aus paint)

    juce::Rectangle<float> takesBadgeBounds() const;
    void showTakesMenu();

    bool                            thumbnailLoaded { false };
    juce::File                      thumbnailFile;   ///< Quelle der aktuell gezeigten Wellenform (Take-/Comp-Wechsel)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipComponent)
};
