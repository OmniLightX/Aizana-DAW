#include "ArrangementView.h"
#include <set>

namespace te = tracktion::engine;

//==============================================================================
// RulerStrip

ArrangementView::RulerStrip::RulerStrip (ApplicationState& appState_,
                                          double& pixelsPerSecond_,
                                          double& scrollPositionSecs_,
                                          double& gridResolution_,
                                          bool&   snapEnabled_)
    : appState           (appState_)
    , pixelsPerSecond    (pixelsPerSecond_)
    , scrollPositionSecs (scrollPositionSecs_)
    , gridResolution     (gridResolution_)
    , snapEnabled        (snapEnabled_)
{
}

void ArrangementView::RulerStrip::paint (juce::Graphics& g)
{
    // Dark background — almost black, just slightly lighter than track area
    g.fillAll (juce::Colour (0xff191919));

    // Bottom separator line
    g.setColour (juce::Colour (0xff333333));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    const double bpm = (edit->tempoSequence.getNumTempos() > 0)
                           ? edit->tempoSequence.getTempo (0)->getBpm()
                           : 120.0;

    const int beatsPerBar = (edit->tempoSequence.getNumTimeSigs() > 0)
                                ? edit->tempoSequence.getTimeSig (0)->numerator
                                : 4;

    const double secondsPerBeat = 60.0 / bpm;
    const double secondsPerBar  = secondsPerBeat * beatsPerBar;
    const double barWidthPx     = secondsPerBar * pixelsPerSecond;

    if (barWidthPx <= 0.0) return;

    // Determine first visible bar
    const double startBar = std::floor (scrollPositionSecs / secondsPerBar);
    const int    firstBar = juce::jmax (0, (int)startBar);

    g.setFont (juce::FontOptions (10.0f));

    for (int bar = firstBar; bar < firstBar + (int)(getWidth() / barWidthPx) + 2; ++bar)
    {
        const double barTimeSecs = bar * secondsPerBar;
        const int    x = (int)((barTimeSecs - scrollPositionSecs) * pixelsPerSecond);

        if (x < 0 || x > getWidth()) continue;

        // Bar number tick — full height on bar boundaries
        g.setColour (juce::Colour (0xff555555));
        g.drawVerticalLine (x, 0.0f, (float)getHeight());

        // Bar number label — displayed as 1-indexed
        g.setColour (juce::Colour (0xff888888));
        g.drawText (juce::String (bar + 1), x + 3, 2, 40, getHeight() - 4,
                    juce::Justification::centredLeft, false);

        // Beat ticks (subtler, only if bar is wide enough to show them)
        if (barWidthPx > 40.0)
        {
            const double beatWidthPx = barWidthPx / beatsPerBar;
            for (int beat = 1; beat < beatsPerBar; ++beat)
            {
                const int bx = x + (int)(beat * beatWidthPx);
                if (bx >= 0 && bx <= getWidth())
                {
                    g.setColour (juce::Colour (0xff333333));
                    g.drawVerticalLine (bx, (float)getHeight() / 2.0f, (float)getHeight());
                }
            }
        }
    }

    // Snap grid lines — faint ticks at the snap resolution, drawn only when snap is on
    // and the resolution is finer than one beat. Only rendered when grid divisions are
    // at least 4px wide to avoid a visual blur of lines.
    if (snapEnabled && gridResolution > 0.0)
    {
        const double gridWidthPx = gridResolution * pixelsPerSecond;
        if (gridWidthPx >= 4.0)
        {
            const double startGrid = std::floor (scrollPositionSecs / gridResolution);
            const int    firstGrid = juce::jmax (0, (int)startGrid);
            const int    numGrids  = (int)(getWidth() / gridWidthPx) + 2;
            g.setColour (juce::Colour (0xff7acc55));  // bright green — clearly visible against dark background
            for (int i = firstGrid; i < firstGrid + numGrids; ++i)
            {
                const double gridTimeSecs = i * gridResolution;
                const int    gx = (int)((gridTimeSecs - scrollPositionSecs) * pixelsPerSecond);
                if (gx < 0 || gx > getWidth()) continue;
                // Draw in lower half so they don't cover bar-number text
                g.drawVerticalLine (gx, getHeight() * 0.4f, (float)getHeight());
            }
        }
    }

    // Draw loop region overlay
    {
        const auto loopInX  = (float)((loopInSecs  - scrollPositionSecs) * pixelsPerSecond);
        const auto loopOutX = (float)((loopOutSecs - scrollPositionSecs) * pixelsPerSecond);

        if (loopOutX > loopInX)
        {
            g.setColour (juce::Colour (0x224488ff));  // translucent blue region
            g.fillRect (loopInX, 0.0f, loopOutX - loopInX, (float)getHeight());
        }

        // Loop In marker — green vertical bar at bottom
        g.setColour (juce::Colour (0xff44bb44));
        g.fillRect (juce::Rectangle<float> (loopInX - 1.0f, (float)(getHeight() - 10), 2.0f, 10.0f));

        // Loop Out marker — red vertical bar at bottom
        g.setColour (juce::Colour (0xffbb4444));
        g.fillRect (juce::Rectangle<float> (loopOutX - 1.0f, (float)(getHeight() - 10), 2.0f, 10.0f));
    }
}

void ArrangementView::RulerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    // Check if click is near a loop marker (within 8px) — loop marker drag takes priority
    const double inX  = (loopInSecs  - scrollPositionSecs) * pixelsPerSecond;
    const double outX = (loopOutSecs - scrollPositionSecs) * pixelsPerSecond;

    if (std::abs ((double)e.x - inX) < 8.0)
    {
        dragging = DragTarget::LoopIn;
        return;
    }
    if (std::abs ((double)e.x - outX) < 8.0)
    {
        dragging = DragTarget::LoopOut;
        return;
    }

    // Default: click-to-seek
    dragging = DragTarget::Seek;
    const double clickTimeSecs = juce::jmax (
        0.0,
        scrollPositionSecs + (double)e.x / pixelsPerSecond);

    edit->getTransport().setPosition (
        tracktion::TimePosition::fromSeconds (clickTimeSecs));
}

void ArrangementView::RulerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == DragTarget::Seek)
    {
        // Scrub playhead
        auto* edit = appState.editManager.getEdit();
        if (edit == nullptr) return;
        const double timeSecs = juce::jmax (0.0, scrollPositionSecs + (double)e.x / pixelsPerSecond);
        edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (timeSecs));
        return;
    }

    if (dragging == DragTarget::None) return;

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    const double timeSecs = juce::jmax (0.0, scrollPositionSecs + (double)e.x / pixelsPerSecond);

    if (dragging == DragTarget::LoopIn)
    {
        loopInSecs = juce::jmin (timeSecs, loopOutSecs - 0.1);
        edit->getTransport().setLoopRange (
            tracktion::TimeRange (
                tracktion::TimePosition::fromSeconds (loopInSecs),
                tracktion::TimePosition::fromSeconds (loopOutSecs)));
    }
    else if (dragging == DragTarget::LoopOut)
    {
        loopOutSecs = juce::jmax (timeSecs, loopInSecs + 0.1);
        edit->getTransport().setLoopRange (
            tracktion::TimeRange (
                tracktion::TimePosition::fromSeconds (loopInSecs),
                tracktion::TimePosition::fromSeconds (loopOutSecs)));
    }

    repaint();
}

void ArrangementView::RulerStrip::mouseUp (const juce::MouseEvent&)
{
    dragging = DragTarget::None;
}

juce::MouseCursor ArrangementView::RulerStrip::getMouseCursor()
{
    // Left-right resize cursor signals "this is an interactive scrub target"
    // — matches Logic Pro and Ableton Live conventions.
    return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

//==============================================================================
// ToolSelectorStrip

ArrangementView::ToolSelectorStrip::ToolSelectorStrip (EditTool& activeTool_,
                                                        std::function<void(EditTool)> onToolSelected_)
    : activeTool     (activeTool_)
    , onToolSelected (std::move (onToolSelected_))
{
}

void ArrangementView::ToolSelectorStrip::paint (juce::Graphics& g)
{
    // Dark background matching header strip
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Single Scissors button — tools are keyboard-only; this button is a visual
    // indicator and toggle for the Scissors tool.  Pointer (default) shows it unlit.
    constexpr int kButtonW = 36;
    constexpr int kButtonH = 20;
    const int     stripH   = getHeight();
    const int     yOff     = (stripH - kButtonH) / 2;

    const juce::Rectangle<int> r (0, yOff, kButtonW, kButtonH);
    const bool scissorsActive = (activeTool == EditTool::Scissors);

    if (scissorsActive)
    {
        g.setColour (juce::Colour (0xff4488ff).withAlpha (0.40f));
        g.fillRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f);
    }

    g.setColour (scissorsActive ? juce::Colour (0xffffffff) : juce::Colour (0xff888888));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("C", r, juce::Justification::centred, false);
}

void ArrangementView::ToolSelectorStrip::mouseDown (const juce::MouseEvent& e)
{
    // Single Scissors button: clicking toggles Scissors ↔ Pointer.
    juce::ignoreUnused (e);
    onToolSelected (activeTool == EditTool::Scissors ? EditTool::Pointer : EditTool::Scissors);
}

//==============================================================================
// Snap-to-grid helper

double ArrangementView::snapTimeToGrid (double timeSecs, bool altDown) const noexcept
{
    if (!snapEnabled || altDown)
        return timeSecs;

    // Recompute resolution dynamically from the current BPM so snap is always correct
    // even after a tempo change (gridResolution is pre-computed and can become stale).
    auto* edit = appState.editManager.getEdit();
    const double bpm = (edit != nullptr && edit->tempoSequence.getNumTempos() > 0)
                           ? edit->tempoSequence.getTempo (0)->getBpm()
                           : 120.0;
    const double beatSecs = 60.0 / bpm;
    double resolution = beatSecs;  // default: 1/4 note
    switch (gridCombo.getSelectedId())
    {
        case 1:  resolution = beatSecs * 4.0;   break;  // 1/1 bar
        case 2:  resolution = beatSecs * 2.0;   break;  // 1/2 note
        case 3:  resolution = beatSecs * 1.0;   break;  // 1/4 note
        case 4:  resolution = beatSecs * 0.5;   break;  // 1/8 note
        case 5:  resolution = beatSecs * 0.25;  break;  // 1/16 note
        default: break;
    }
    if (resolution <= 0.0)
        return timeSecs;
    return std::round (timeSecs / resolution) * resolution;
}

double ArrangementView::snapDeltaToGrid (double deltaSecs, bool altDown) const noexcept
{
    if (!snapEnabled || altDown)
        return deltaSecs;

    auto* edit = appState.editManager.getEdit();
    const double bpm = (edit != nullptr && edit->tempoSequence.getNumTempos() > 0)
                           ? edit->tempoSequence.getTempo (0)->getBpm()
                           : 120.0;
    const double beatSecs = 60.0 / bpm;
    double resolution = beatSecs;
    switch (gridCombo.getSelectedId())
    {
        case 1:  resolution = beatSecs * 4.0;   break;  // 1/1 bar
        case 2:  resolution = beatSecs * 2.0;   break;  // 1/2 note
        case 3:  resolution = beatSecs * 1.0;   break;  // 1/4 note
        case 4:  resolution = beatSecs * 0.5;   break;  // 1/8 note
        case 5:  resolution = beatSecs * 0.25;  break;  // 1/16 note
        default: break;
    }
    if (resolution <= 0.0)
        return deltaSecs;
    return std::round (deltaSecs / resolution) * resolution;
}

//==============================================================================

ArrangementView::ArrangementView (ApplicationState&    appState_,
                                  ClipEditor&          clipEditor_,
                                  MixerEngine&         mixerEngine_,
                                  PluginManager&       pluginManager_,
                                  PluginWindowManager& pluginWindowManager_,
                                  PluginScanner&       pluginScanner_,
                                  AutomationEngine&    automationEngine_)
    : appState            (appState_)
    , clipEditor          (clipEditor_)
    , mixerEngine         (mixerEngine_)
    , pluginManager       (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner       (pluginScanner_)
    , automationEngine    (automationEngine_)
    , rulerStrip          (appState_, pixelsPerSecond, scrollPositionSecs, gridResolution, snapEnabled)
    , toolSelector        (activeTool, [this] (EditTool t) { setActiveTool (t); })
{
    // Header viewport — scrolls vertically only (no horizontal scroll)
    headerViewport.setScrollBarsShown (true, false);
    headerViewport.setViewedComponent (&headerContainer, false);
    addAndMakeVisible (headerViewport);

    // Timeline viewport — scrolls both horizontally and vertically
    timelineViewport.setScrollBarsShown (true, true);
    timelineViewport.setViewedComponent (&timelineContainer, false);
    // Disable JUCE 8 drag-to-scroll: the DAW controls scrolling explicitly.
    // Without this, the Viewport's DragToScrollListener can steal mouse events
    // from ClipComponents (e.g., double-click to open Piano Roll).
    timelineViewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible (timelineViewport);

    // Keep vertical scroll positions in sync between header and timeline.
    // juce::ScrollBar does not have onValueChange — must use addListener().
    timelineViewport.getVerticalScrollBar().addListener (this);
    headerViewport.getVerticalScrollBar().addListener (this);

    // Horizontal scroll listener — keeps scrollPositionSecs in sync with ruler
    timelineViewport.getHorizontalScrollBar().addListener (this);

    // Add Track button — header strip, top-left
    addAndMakeVisible (addTrackButton);
    addTrackButton.setTooltip ("Add a new track (Cmd+T)");
    addTrackButton.onClick = [this]
    {
        auto* edit = this->appState.editManager.getEdit();
        if (edit == nullptr) return;

        juce::PopupMenu menu;
        menu.addItem (1, "Audio Track");
        menu.addItem (2, "MIDI Track");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton),
            [this, edit] (int result)
            {
                if (result == 0) return;  // dismissed

                auto& undoManager = this->appState.editManager.getUndoManager();

                // Insert AFTER the last existing track so existing clips stay on their tracks.
                // TrackInsertPoint(nullptr, nullptr) inserts at the beginning — wrong.
                // TrackInsertPoint(nullptr, lastTrack) inserts after lastTrack — correct.
                auto existingTracks = te::getAudioTracks (*edit);
                te::AudioTrack* lastExistingTrack = existingTracks.isEmpty()
                    ? nullptr : existingTracks.getLast();

                if (result == 1)
                {
                    // Audio track
                    undoManager.beginNewTransaction ("Add Audio Track");
                    auto newTrack = edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, lastExistingTrack), nullptr);
                    // Clear solo-isolated so solo shortcuts work correctly on this track.
                    // Bake the default name into state so it is never re-derived from position
                    // after a drag-reorder (TE generates "Track N" lazily from position).
                    if (newTrack != nullptr)
                    {
                        newTrack->setSoloIsolate (false);
                        newTrack->setName (newTrack->getName());
                    }
                    // ArrangementView's ValueTree listener (valueTreeChildAdded) will
                    // automatically call rebuildTrackRows() — no manual update needed
                }
                else if (result == 2)
                {
                    // MIDI track: create an AudioTrack and insert an empty MIDI clip
                    undoManager.beginNewTransaction ("Add MIDI Track");
                    te::AudioTrack::Ptr newTrack = edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, lastExistingTrack), nullptr);

                    if (newTrack != nullptr)
                    {
                        newTrack->setSoloIsolate (false);
                        newTrack->setName ("MIDI " + juce::String (te::getAudioTracks (*edit).size()));

                        // Insert a 4-bar MIDI clip so the user has something to double-click
                        const double bpm = (edit->tempoSequence.getNumTempos() > 0)
                                             ? edit->tempoSequence.getTempo (0)->getBpm()
                                             : 120.0;
                        const double barLengthSecs  = 60.0 / bpm * 4.0;   // 4 beats per bar
                        const double clipLengthSecs = barLengthSecs * 4.0; // 4 bars

                        te::ClipPosition midiPos {
                            tracktion::TimeRange (
                                tracktion::TimePosition::fromSeconds (0.0),
                                tracktion::TimePosition::fromSeconds (clipLengthSecs)),
                            tracktion::TimeDuration()
                        };

                        newTrack->insertNewClip (
                            te::TrackItem::Type::midi,
                            "MIDI Clip",
                            midiPos,
                            nullptr);
                    }
                }
            });
    };

    // Zoom buttons — header strip, top-right corner
    // These provide discoverability and a keyboard-alternative to pinch/Ctrl+scroll.
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
    zoomInButton.setTooltip  ("Zoom in (Ctrl + scroll up)");
    zoomOutButton.setTooltip ("Zoom out (Ctrl + scroll down)");

    // No border or fill — styled to match the dark header strip
    auto styleZoomButton = [] (juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0x00000000));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x00000000));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaaaaaa));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
    };
    styleZoomButton (zoomInButton);
    styleZoomButton (zoomOutButton);

    zoomInButton.onClick  = [this] { setPixelsPerSecond (pixelsPerSecond * 1.4); };
    zoomOutButton.onClick = [this] { setPixelsPerSecond (pixelsPerSecond / 1.4); };

    // Wire ZoomViewport callback — intercepts Ctrl/Cmd+scroll over the timeline.
    // ZoomViewport routes ALL modifier+scroll events here, so we must branch
    // explicitly: Cmd+scroll = vertical zoom, Ctrl+scroll = horizontal zoom.
    // Delegating back to mouseWheelMove was unreliable because JUCE deltaX/deltaY
    // dominance heuristics differ per platform; handling both paths here is direct.
    timelineViewport.onZoomWheel = [this] (const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& w)
    {
        // Vertical zoom: Cmd+scroll (Logic Pro convention).
        // On macOS Cmd key sets isCommandDown; trackpad pinch sets isCtrlDown (synthetic).
        // The isCommandDown && !isCtrlDown guard keeps the two paths mutually exclusive.
        if (e.mods.isCommandDown() && !e.mods.isCtrlDown())
        {
            const double zoomFactor = 1.0 + w.deltaY * 2.0;
            trackHeightScale = juce::jlimit (0.5, 3.0, trackHeightScale * zoomFactor);
            resized();
            repaint();
            return;
        }

        // Horizontal zoom: Ctrl+scroll or trackpad pinch (macOS synthetic Ctrl).
        // Convert event to ArrangementView-local coords for zoom-to-cursor math.
        const auto eventInAV = e.getEventRelativeTo (this);
        const double zoomFactor = 1.0 + w.deltaY * 2.0;
        const double newPPS = juce::jlimit (10.0, 2000.0, pixelsPerSecond * zoomFactor);

        // Zoom towards the mouse cursor: keep the time under the cursor fixed.
        const int mouseXInTimeline = eventInAV.getPosition().x - kHeaderWidth
                                     + timelineViewport.getViewPositionX();
        const double timeAtCursor = mouseXInTimeline / pixelsPerSecond;

        pixelsPerSecond = newPPS;

        // resized() FIRST so timelineContainer is sized for the new zoom level.
        // setViewPosition after resize so the viewport can actually reach newScrollX
        // without being clamped by a still-undersized content area.
        resized();

        const int newMouseXInTimeline = (int)(timeAtCursor * pixelsPerSecond);
        const int newScrollX = newMouseXInTimeline - (eventInAV.getPosition().x - kHeaderWidth);
        timelineViewport.setViewPosition (juce::jmax (0, newScrollX),
                                          timelineViewport.getViewPositionY());

        // Read back the actual scroll position (handles any residual clamping),
        // so ruler and clips always read the same scrollPositionSecs.
        scrollPositionSecs = (double)timelineViewport.getViewPositionX() / pixelsPerSecond;

        rulerStrip.repaint();
        repaint();
    };

    // Wire pinch-to-zoom callback — macOS trackpad pinch gesture fires
    // magnificationChanged (not mouseWheelMove) in JUCE. scaleFactor > 1.0 = zoom in,
    // < 1.0 = zoom out. Clamped to the same range as Ctrl+scroll zoom [10, 2000] pps.
    timelineViewport.onPinchZoom = [this] (double scaleFactor)
    {
        const double newPPS = juce::jlimit (10.0, 2000.0, pixelsPerSecond * scaleFactor);
        setPixelsPerSecond (newPPS);
    };

    // Ruler strip — above timeline rows, aligned with timeline panel (not headers)
    addAndMakeVisible (rulerStrip);

    // Tool selector strip — visible in header area between Add Track and zoom buttons
    addAndMakeVisible (toolSelector);

    // ---- Snap-to-grid controls ----
    // Snap button toggles snap on/off. Uses manual toggle-state management
    // (setClickingTogglesState(false)) to avoid JUCE double-toggle race condition
    // (plan 01-49 invariant: onClick fires AFTER auto-toggle when setClickingTogglesState(true)).
    snapButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
    snapButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4488ff));
    snapButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaaaaaa));
    snapButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
    snapButton.setClickingTogglesState (false);
    snapButton.setToggleState (snapEnabled, juce::dontSendNotification);
    snapButton.onClick = [this]
    {
        snapEnabled = !snapEnabled;
        snapButton.setToggleState (snapEnabled, juce::dontSendNotification);
        snapButton.repaint();
    };
    addAndMakeVisible (snapButton);

    // Grid resolution ComboBox — 1/1 bar through 1/16 at current BPM.
    // IDs 1-5 map to 1/1, 1/2, 1/4, 1/8, 1/16.
    gridCombo.addItem ("1/1",  1);
    gridCombo.addItem ("1/2",  2);
    gridCombo.addItem ("1/4",  3);
    gridCombo.addItem ("1/8",  4);
    gridCombo.addItem ("1/16", 5);
    gridCombo.setSelectedId (3, juce::dontSendNotification);  // default 1/4 note
    gridCombo.onChange = [this]
    {
        auto* edit = appState.editManager.getEdit();
        const double bpm = (edit != nullptr && edit->tempoSequence.getNumTempos() > 0)
                               ? edit->tempoSequence.getTempo (0)->getBpm()
                               : 120.0;
        const double beatSecs = 60.0 / bpm;
        switch (gridCombo.getSelectedId())
        {
            case 1:  gridResolution = beatSecs * 4.0;   break;  // 1/1 bar (4/4)
            case 2:  gridResolution = beatSecs * 2.0;   break;  // 1/2
            case 3:  gridResolution = beatSecs * 1.0;   break;  // 1/4
            case 4:  gridResolution = beatSecs * 0.5;   break;  // 1/8
            case 5:  gridResolution = beatSecs * 0.25;  break;  // 1/16
            default: gridResolution = beatSecs;          break;
        }
        rulerStrip.repaint();  // redraw grid lines at new resolution
    };
    // Trigger initial resolution calculation
    gridCombo.onChange();
    addAndMakeVisible (gridCombo);

    // Autoscroll toggle — F key / toolbar button.
    // Same style as snapButton: blue when ON, grey when OFF.
    autoScrollButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
    autoScrollButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4488ff));
    autoScrollButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaaaaaa));
    autoScrollButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
    autoScrollButton.setClickingTogglesState (false);
    autoScrollButton.setToggleState (autoScrollEnabled, juce::dontSendNotification);
    autoScrollButton.setTooltip ("Follow playhead during playback (F)");
    autoScrollButton.onClick = [this]
    {
        autoScrollEnabled = !autoScrollEnabled;
        autoScrollButton.setToggleState (autoScrollEnabled, juce::dontSendNotification);
        autoScrollButton.repaint();
    };
    addAndMakeVisible (autoScrollButton);

    // Keyboard focus: any mouse click in the arrangement area reclaims focus so
    // E/R/S tool shortcuts work reliably without requiring manual tab navigation.
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    // Register as ValueTree listener on the Edit's state so we rebuild when
    // tracks are added/removed — RESEARCH.md Pattern 1 (reactive ValueTree).
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    // Build initial track rows
    rebuildTrackRows();

    // 30ms timer drives playhead position updates (~33 fps, smooth enough for playback)
    startTimer (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();

    timelineViewport.getVerticalScrollBar().removeListener (this);
    headerViewport.getVerticalScrollBar().removeListener (this);
    timelineViewport.getHorizontalScrollBar().removeListener (this);

    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();

    // Header strip at top: Add Track button (height 28px)
    auto headerStrip = area.removeFromTop (kHeaderStripHeight);
    addTrackButton.setBounds (headerStrip.removeFromLeft (80).reduced (2));

    // Tool selector strip — 3 tools × 36px = 108px, immediately right of Add Track button
    toolSelector.setBounds (headerStrip.removeFromLeft (108).reduced (2));

    // Zoom buttons — top-right of the header strip, 20×20 each with 4px gap
    // Place right-to-left: zoomIn (+) rightmost, then zoomOut (−) to its left.
    zoomInButton.setBounds  (getWidth() - 28, headerStrip.getY() + 4, 20, 20);
    zoomOutButton.setBounds (getWidth() - 52, headerStrip.getY() + 4, 20, 20);

    // Snap controls — to the right of the tool selector, before the zoom buttons.
    const int snapBtnX = 80 + 4 + 108 + 4;  // addTrackButton(80) + gap(4) + toolSelector(108) + gap(4)
    snapButton.setBounds (snapBtnX, headerStrip.getY() + 4, 40, 20);
    gridCombo.setBounds  (snapBtnX + 44, headerStrip.getY() + 4, 56, 20);

    // Autoscroll button — immediately right of gridCombo
    autoScrollButton.setBounds (snapBtnX + 44 + 60, headerStrip.getY() + 4, 28, 20);

    // Ruler strip — full width, sits between header strip and track rows
    // The ruler portion under the header column (left of kHeaderWidth) is filled
    // by paint() to match the ruler colour, keeping alignment clean.
    auto rulerArea = area.removeFromTop (kRulerHeight);
    rulerStrip.setBounds (rulerArea.withTrimmedLeft (kHeaderWidth));

    // Left: track headers panel
    headerViewport.setBounds (area.removeFromLeft (kHeaderWidth));

    // Right: timeline panel (rest of width)
    timelineViewport.setBounds (area);

    // Timeline width: 5 minutes at current zoom, or at least the viewport width
    const double timelineWidthPx = pixelsPerSecond * 300.0;   // 5 minutes

    // Lay out track headers with variable heights matching their corresponding TrackRow.
    // When an automation lane is expanded the header must grow to match, so both
    // columns stay aligned while scrolling vertically in sync.
    int headerY = 0;
    for (int i = 0; i < trackHeaders.size() && i < trackRows.size(); ++i)
    {
        const int rowH = (int)(trackRows[i]->getPreferredHeight() * trackHeightScale);
        trackHeaders[i]->setBounds (0, headerY, kHeaderWidth, rowH);
        headerY += rowH;
    }

    // Update headerContainer height to match total track row height
    const int totalHeaderH = headerY;
    headerContainer.setSize (kHeaderWidth,
                             juce::jmax (totalHeaderH, headerViewport.getHeight()));

    // Lay out TrackRows in the timeline container
    layoutTrackRows();

    // Timeline container height = sum of all TrackRow preferred heights (scaled)
    int totalRowH = 0;
    for (auto* row : trackRows)
        totalRowH += (int)(row->getPreferredHeight() * trackHeightScale);

    timelineContainer.setSize (juce::jmax ((int)timelineWidthPx, timelineViewport.getWidth()),
                               juce::jmax (totalRowH, timelineViewport.getHeight()));
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Fill the ruler-height column on the left (under the header column) to
    // match the ruler strip colour, so the two form a visually continuous strip.
    g.setColour (juce::Colour (0xff191919));
    g.fillRect (0, kHeaderStripHeight, kHeaderWidth, kRulerHeight);

    // Draw a blue highlight border when audio files are being dragged over
    if (isDragHovering)
    {
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.4f));
        g.fillRect (getLocalBounds());
        g.setColour (juce::Colours::dodgerblue);
        g.drawRect (getLocalBounds(), 2);
    }

    // Drop indicator: thin Apple system blue line between rows during track drag-reorder
    if (dragOverRowIndex >= 0)
    {
        // Calculate cumulative height of rows 0..dragOverRowIndex-1 to get correct Y
        int cumulativeRowH = 0;
        for (int i = 0; i < dragOverRowIndex && i < trackRows.size(); ++i)
            cumulativeRowH += (int)(trackRows[i]->getPreferredHeight() * trackHeightScale);
        const int indicatorY = kHeaderStripHeight + kRulerHeight + cumulativeRowH;
        g.setColour (juce::Colour (0xff3d9eff));  // Apple system blue accent
        g.fillRect (0, indicatorY - 1, getWidth(), 2);
    }

    // ---- Selected clip highlights ----
    if (! selectedClips.isEmpty())
    {
        const int timelineLeft = kHeaderWidth;
        const int timelineTop  = kHeaderStripHeight + kRulerHeight;

        g.setColour (juce::Colours::dodgerblue.withAlpha (0.25f));
        for (auto* cc : selectedClips)
        {
            if (cc == nullptr) continue;
            auto* row = cc->getParentComponent();   // clipArea
            if (row == nullptr || row->getParentComponent() == nullptr) continue;
            auto* trackRow = row->getParentComponent();  // TrackRow

            auto bounds = cc->getBounds()
                .translated (row->getX() + trackRow->getX(),
                             row->getY() + trackRow->getY())
                .translated (timelineLeft - timelineViewport.getViewPositionX(),
                             timelineTop  - timelineViewport.getViewPositionY());

            g.fillRect (bounds);
        }
    }
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    // Drawn AFTER all child components (clips, tracks) so all overlays are
    // always visible on top — paint() would be invisible behind the opaque TrackRows.

    // ---- Playhead — drawn first so it goes under selection overlays if both are visible ----
    // Must be in paintOverChildren (not paint) so it renders ON TOP of ClipComponents.
    if (appState.editManager.getEdit() != nullptr)
    {
        const int absolutePlayheadX = (int)(playheadPositionSecs * pixelsPerSecond);
        const int screenX = kHeaderWidth + absolutePlayheadX - timelineViewport.getViewPositionX();
        const int rulerBottom = kHeaderStripHeight + kRulerHeight;

        if (screenX >= kHeaderWidth && screenX <= getWidth())
        {
            g.setColour (juce::Colour (0xffd0d0d0).withAlpha (0.85f));
            g.drawVerticalLine (screenX, (float)rulerBottom, (float)getHeight());
        }
    }

    // ---- Time-range highlight (Marquee tool / Cmd+drag / Click Zone) ----
    // Constrained to the selected track row band; shown as a tinted region with
    // solid boundary lines (Studio One / Pro Tools style).
    if (rangeActive && rangeEndSecs > rangeStartSecs && !trackRows.isEmpty())
    {
        // Use timelineViewport.getViewPositionX() (always current) instead of
        // scrollPositionSecs (cached member that can lag after zoom-induced reposition).
        // This matches the playhead drawing pattern for pixel-accurate alignment.
        const int x1 = kHeaderWidth + (int)(rangeStartSecs * pixelsPerSecond) - timelineViewport.getViewPositionX();
        const int x2 = kHeaderWidth + (int)(rangeEndSecs   * pixelsPerSecond) - timelineViewport.getViewPositionX();
        const int viewScrollY    = timelineViewport.getViewPositionY();
        const int timelineOriginY = kHeaderStripHeight + kRulerHeight;

        int cumY = 0;
        for (int i = 0; i < rangeTrackStart && i < trackRows.size(); ++i)
            cumY += (int)(trackRows[i]->getPreferredHeight() * trackHeightScale);
        const int trackTopPx = timelineOriginY + cumY - viewScrollY;

        int tracksBotCumY = cumY;
        for (int i = rangeTrackStart; i <= rangeTrackEnd && i < trackRows.size(); ++i)
            tracksBotCumY += (int)(trackRows[i]->getPreferredHeight() * trackHeightScale);
        const int trackBotPx = timelineOriginY + tracksBotCumY - viewScrollY;

        // Translucent tint — visible on top of clips
        g.setColour (juce::Colour (0x554488ff));
        g.fillRect (x1, trackTopPx, x2 - x1, trackBotPx - trackTopPx);

        // Solid boundary lines
        g.setColour (juce::Colour (0xff4488ff));
        g.drawVerticalLine (x1, (float)trackTopPx, (float)trackBotPx);
        g.drawVerticalLine (x2, (float)trackTopPx, (float)trackBotPx);
    }

    // ---- Live recording placeholder ----
    // Tracktion Engine commits the real WaveAudioClip when recording stops. During
    // capture we draw a non-persistent recording region so the arrangement behaves
    // like a professional DAW without mutating clip state on every audio block.
    if (auto* edit = appState.editManager.getEdit())
    {
        if (edit->getTransport().isRecording()
            && playheadPositionSecs > recordingOverlayStartSecs
            && ! trackRows.isEmpty())
        {
            const int x1 = kHeaderWidth + (int)(recordingOverlayStartSecs * pixelsPerSecond)
                           - timelineViewport.getViewPositionX();
            const int x2 = kHeaderWidth + (int)(playheadPositionSecs * pixelsPerSecond)
                           - timelineViewport.getViewPositionX();
            const int viewScrollY    = timelineViewport.getViewPositionY();
            const int timelineOriginY = kHeaderStripHeight + kRulerHeight;

            int cumY = 0;
            for (auto* row : trackRows)
            {
                const int rowH = (int)(row->getPreferredHeight() * trackHeightScale);
                const bool armed = (bool) row->getTrack().state.getProperty ("armed", false);

                if (armed)
                {
                    const int top = timelineOriginY + cumY - viewScrollY + 4;
                    const int bottom = timelineOriginY + cumY + rowH - viewScrollY - 4;
                    const auto rect = juce::Rectangle<int> (x1, top,
                                                            juce::jmax (2, x2 - x1),
                                                            juce::jmax (8, bottom - top));

                    if (rect.getRight() >= kHeaderWidth && rect.getX() <= getWidth())
                    {
                        g.setColour (juce::Colour (0x883a9bdc));
                        g.fillRoundedRectangle (rect.toFloat(), 3.0f);
                        g.setColour (juce::Colour (0xff56c6f0));
                        g.drawRoundedRectangle (rect.toFloat().reduced (0.5f), 3.0f, 1.0f);
                    }
                }

                cumY += rowH;
            }
        }
    }

    // ---- Marquee selection rectangle (RangeSelect tool) ----
    if (isMarqueeActive && ! marqueeRect.isEmpty())
    {
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.25f));
        g.fillRect (marqueeRect);
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.8f));
        g.drawRect (marqueeRect, 1);
    }

    // ---- Pointer rubber-band (Arrow tool clip selection) ----
    // Thin blue outline + very light fill — Studio One rubber-band style.
    if (pointerBandActive && ! pointerBandRect.isEmpty())
    {
        g.setColour (juce::Colour (0x224488ff));
        g.fillRect (pointerBandRect);
        g.setColour (juce::Colour (0xff4488ff));
        g.drawRect (pointerBandRect, 1);
    }
}

void ArrangementView::setPixelsPerSecond (double pps)
{
    pixelsPerSecond = juce::jlimit (10.0, 2000.0, pps);
    resized();
    // Sync scrollPositionSecs from the viewport's actual position after resize.
    // pixelsPerSecond just changed, so the old cached value is wrong:
    // scrollPositionSecs must always == getViewPositionX() / pixelsPerSecond.
    scrollPositionSecs = (double)timelineViewport.getViewPositionX() / pixelsPerSecond;
    rulerStrip.repaint();
    repaint();
    // Reposition automation lane controls for new zoom level
    for (auto* row : trackRows)
        row->updateScrollOffset();
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel)
{
    // Pinch-to-zoom: macOS trackpad delivers pinch as Ctrl+scroll-Y
    // Also handle Cmd+scroll for explicit keyboard+scroll zoom
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        // Vertical zoom: Cmd+scroll when deltaY is dominant (Logic Pro convention)
        if (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) && event.mods.isCommandDown())
        {
            const double zoomFactor = 1.0 + wheel.deltaY * 2.0;
            trackHeightScale = juce::jlimit (0.5, 3.0, trackHeightScale * zoomFactor);
            resized();
            repaint();
            return;
        }

        // Horizontal zoom: Ctrl+scroll or Cmd+horizontal scroll
        // deltaY > 0 = zoom in, deltaY < 0 = zoom out
        const double zoomFactor = 1.0 + wheel.deltaY * 2.0;
        const double newPPS = juce::jlimit (10.0, 2000.0, pixelsPerSecond * zoomFactor);

        // Zoom towards the mouse cursor position:
        // Calculate the time position under the cursor before zoom
        const int mouseXInTimeline = event.getPosition().x - kHeaderWidth
                                     + timelineViewport.getViewPositionX();
        const double timeAtCursor = mouseXInTimeline / pixelsPerSecond;

        // Apply new zoom
        pixelsPerSecond = newPPS;

        // resized() FIRST so content is sized for new zoom before scrolling.
        resized();

        // Scroll so the same time position remains under the cursor
        const int newMouseXInTimeline = (int)(timeAtCursor * pixelsPerSecond);
        const int newScrollX = newMouseXInTimeline - (event.getPosition().x - kHeaderWidth);
        timelineViewport.setViewPosition (juce::jmax (0, newScrollX),
                                          timelineViewport.getViewPositionY());

        // Read back actual scroll (handles clamping); ruler and clips share this value.
        scrollPositionSecs = (double)timelineViewport.getViewPositionX() / pixelsPerSecond;

        rulerStrip.repaint();
        repaint();
        return;
    }

    // Default: pass to base class for normal scrolling
    Component::mouseWheelMove (event, wheel);
}

//==============================================================================
// Timer — 30ms playhead poll

void ArrangementView::timerCallback()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    const bool isRecording = edit->getTransport().isRecording();
    const double newPos = edit->getTransport().getPosition().inSeconds();

    if (isRecording && ! wasRecordingLastTick)
        recordingOverlayStartSecs = newPos;
    else if (! isRecording && wasRecordingLastTick)
        juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });

    wasRecordingLastTick = isRecording;

    if (std::abs (newPos - playheadPositionSecs) > 0.001)
    {
        playheadPositionSecs = newPos;
        // Repaint timeline container and ruler (not headers — avoids layout flicker)
        timelineContainer.repaint();
        rulerStrip.repaint();
        repaint();  // repaints the playhead line (paintOverChildren — always on top of clips)
    }

    // Auto-follow playhead during playback (only when autoscroll is enabled)
    if (autoScrollEnabled && edit->getTransport().isPlaying())
    {
        const double viewportWidthSecs = timelineViewport.getWidth() / pixelsPerSecond;
        const double visibleStartSecs = scrollPositionSecs;

        // If playhead is past 80% of visible area, scroll to put playhead at 20%
        const double scrollThreshold = visibleStartSecs + viewportWidthSecs * 0.8;
        if (playheadPositionSecs > scrollThreshold)
        {
            const double newStartSecs = playheadPositionSecs - viewportWidthSecs * 0.2;
            const int newScrollX = (int)(juce::jmax (0.0, newStartSecs) * pixelsPerSecond);
            timelineViewport.setViewPosition (newScrollX, timelineViewport.getViewPositionY());
            // CRITICAL: update scrollPositionSecs manually since setViewPosition
            // does not trigger scrollBarMoved for programmatic calls
            scrollPositionSecs = juce::jmax (0.0, newStartSecs);
            rulerStrip.repaint();
        }

        // Also handle playhead scrolling left (e.g., loop back to start)
        if (playheadPositionSecs < visibleStartSecs)
        {
            const double newStartSecs = juce::jmax (0.0, playheadPositionSecs - 0.5);
            const int newScrollX = (int)(newStartSecs * pixelsPerSecond);
            timelineViewport.setViewPosition (newScrollX, timelineViewport.getViewPositionY());
            scrollPositionSecs = newStartSecs;
            rulerStrip.repaint();
        }
    }
}

//==============================================================================
// Keyboard — arrow key scrubbing

bool ArrangementView::keyPressed (const juce::KeyPress& key)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return false;

    const double bpm = (edit->tempoSequence.getNumTempos() > 0)
                           ? edit->tempoSequence.getTempo (0)->getBpm()
                           : 120.0;

    const int beatsPerBar = (edit->tempoSequence.getNumTimeSigs() > 0)
                                ? edit->tempoSequence.getTimeSig (0)->numerator
                                : 4;

    const double secondsPerBeat = 60.0 / bpm;
    const double secondsPerBar  = secondsPerBeat * beatsPerBar;

    // Bare arrow = 1 beat, Cmd/Ctrl + arrow = 1 bar
    const bool bigNudge = key.getModifiers().isCommandDown()
                       || key.getModifiers().isCtrlDown();
    const double nudge  = bigNudge ? secondsPerBar : secondsPerBeat;

    auto& transport = edit->getTransport();
    const double current = transport.getPosition().inSeconds();

    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        transport.setPosition (tracktion::TimePosition::fromSeconds (current + nudge));
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        transport.setPosition (tracktion::TimePosition::fromSeconds (juce::jmax (0.0, current - nudge)));
        return true;
    }

    // ---- Zoom reset ----
    // Cmd+0: reset track height to default (trackHeightScale = 1.0, 60px per track).
    if (key.getModifiers().isCommandDown()
        && (key.getKeyCode() == '0' || key.getKeyCode() == juce::KeyPress::numberPad0))
    {
        trackHeightScale = 1.0;
        resized();
        repaint();
        return true;
    }

    // ---- Tool shortcuts ----
    // Escape always returns to Pointer and deselects all clips
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        setActiveTool (EditTool::Pointer);
        deselectAllClips();
        rangeActive = false;
        repaint();
        return true;
    }

    // Tab: toggle between Pointer and Marquee (like Studio One)
    if (key.getKeyCode() == juce::KeyPress::tabKey && !key.getModifiers().isAnyModifierKeyDown())
    {
        setActiveTool (activeTool == EditTool::Marquee ? EditTool::Pointer : EditTool::Marquee);
        return true;
    }

    // Letter-key shortcuts (plain keys only, no modifiers):
    // Tool shortcuts:   C = Scissors
    // Track shortcuts:  M = Mute,  S = Solo,  R = Record arm
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (key.getKeyCode() == 'f' || key.getKeyCode() == 'F')
        {
            autoScrollEnabled = !autoScrollEnabled;
            autoScrollButton.setToggleState (autoScrollEnabled, juce::dontSendNotification);
            autoScrollButton.repaint();
            return true;
        }
        if (key.getKeyCode() == 'c' || key.getKeyCode() == 'C')
        {
            setActiveTool (EditTool::Scissors);
            return true;
        }
        if (key.getKeyCode() == 'm' || key.getKeyCode() == 'M')
        {
            if (selectedTrackHeader != nullptr)
            {
                // toggleMute() sets button state directly (no TE read-back) — guarantees visual update.
                selectedTrackHeader->toggleMute();
            }
            else if (! selectedClips.isEmpty())
            {
                // Collect unique tracks from selected clips and toggle mute together
                juce::Array<te::AudioTrack*> uniqueTracks;
                for (auto* cc : selectedClips)
                    if (auto* at = dynamic_cast<te::AudioTrack*> (cc->getClip().getClipTrack()))
                        if (! uniqueTracks.contains (at)) uniqueTracks.add (at);
                // If ANY track is unmuted, mute all; otherwise unmute all
                bool anyUnmuted = false;
                for (auto* t : uniqueTracks) if (! t->isMuted (false)) { anyUnmuted = true; break; }
                for (auto* t : uniqueTracks) t->setMute (anyUnmuted);
            }
            else
            {
                // No selection: toggle mute on all tracks (matches R else-branch behaviour)
                for (auto* h : trackHeaders)
                    h->toggleMute();
            }
            // Sync all TrackHeader visuals (other headers may have changed state)
            for (auto* h : trackHeaders) h->refresh();
            return true;
        }
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            if (selectedTrackHeader != nullptr)
            {
                // toggleSolo() sets button state directly (no TE read-back) — guarantees visual update.
                selectedTrackHeader->toggleSolo();
            }
            else if (! selectedClips.isEmpty())
            {
                // Collect unique tracks from selected clips and toggle solo together
                juce::Array<te::AudioTrack*> uniqueTracks;
                for (auto* cc : selectedClips)
                    if (auto* at = dynamic_cast<te::AudioTrack*> (cc->getClip().getClipTrack()))
                        if (! uniqueTracks.contains (at)) uniqueTracks.add (at);
                bool anySoloed = false;
                for (auto* t : uniqueTracks) if (t->isSolo (false)) { anySoloed = true; break; }
                for (auto* t : uniqueTracks) { t->setSoloIsolate (false); t->setSolo (! anySoloed); }
            }
            else
            {
                // No selection: toggle solo on all tracks (matches R else-branch behaviour)
                for (auto* h : trackHeaders)
                    h->toggleSolo();
            }
            // Sync all TrackHeader visuals
            for (auto* h : trackHeaders) h->refresh();
            return true;
        }
        if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
        {
            if (selectedTrackHeader != nullptr)
            {
                selectedTrackHeader->toggleArm();
                selectedTrackHeader->refresh();
            }
            else
            {
                // Cmd+A path: arm/disarm all tracks simultaneously
                for (auto* h : trackHeaders)
                {
                    h->toggleArm();
                    h->refresh();
                }
            }
            return true;
        }
        if (key.getKeyCode() == 'x' || key.getKeyCode() == 'X')
        {
            applyCrossfadeToSelectedClips();
            return true;
        }
    }

    // Delete key with active time-range: remove the selected region from all clips
    if ((key.getKeyCode() == juce::KeyPress::deleteKey
         || key.getKeyCode() == juce::KeyPress::backspaceKey)
        && rangeActive && rangeEndSecs > rangeStartSecs)
    {
        deleteTimeRange();
        return true;
    }

    // ---- Cmd+A — select all clips on all tracks ----
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'a' || key.getKeyCode() == 'A'))
    {
        // Clear track-header selection so subsequent S/M/R apply to ALL tracks
        // (same behaviour as MixerView: Cmd+A deselects the single strip and selects all).
        setSelectedTrackHeader (nullptr);
        selectedClips.clear();
        for (auto* row : trackRows)
            for (int i = 0; i < row->getClipArea().getNumChildComponents(); ++i)
                if (auto* cc = dynamic_cast<ClipComponent*> (row->getClipArea().getChildComponent (i)))
                {
                    cc->setSelected (true);
                    selectedClips.add (cc);
                }
        repaint();
        return true;
    }

    // ---- Clip clipboard shortcuts (Cmd+C / Cmd+D / Cmd+V) ----

    // Cmd+C — copy selected clip to clipboard
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'c' || key.getKeyCode() == 'C'))
    {
        if (auto* cc = selectedClips.getFirst())
            clipEditor.copyToClipboard (cc->getClip());
        return true;
    }

    // Cmd+D — duplicate selected clip (places copy immediately after original)
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D'))
    {
        if (auto* cc = selectedClips.getFirst())
        {
            clipEditor.duplicateClip (cc->getClip());
            rebuildTrackRows();
        }
        return true;
    }

    // Cmd+V — paste clipboard clip at playhead position
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'v' || key.getKeyCode() == 'V'))
    {
        if (clipEditor.hasClipboardContents())
        {
            if (auto* currentEdit = appState.editManager.getEdit())
            {
                // Paste to the track of the selected clip, or the first audio track
                te::AudioTrack* targetTrack = nullptr;
                if (auto* cc = selectedClips.getFirst())
                    targetTrack = dynamic_cast<te::AudioTrack*> (cc->getClip().getClipTrack());
                if (targetTrack == nullptr)
                {
                    for (auto* t : te::getAudioTracks (*currentEdit))
                    {
                        if (MixerEngine::isAudioChannel (*t))
                        {
                            targetTrack = t;
                            break;
                        }
                    }
                }
                if (targetTrack != nullptr)
                {
                    double pasteTime = currentEdit->getTransport().getPosition().inSeconds();
                    clipEditor.pasteClip (*targetTrack, pasteTime);
                    rebuildTrackRows();
                }
            }
        }
        return true;
    }

    // Cmd+E — split selected clips at playhead (or all clips on all tracks if nothing selected)
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'e' || key.getKeyCode() == 'E'))
    {
        const double playheadSecs = edit->getTransport().getPosition().inSeconds();
        bool didSplit = false;

        auto splitWave = [&] (te::Clip* clip)
        {
            auto* wc = dynamic_cast<te::WaveAudioClip*> (clip);
            if (wc == nullptr) return;
            const double start = clip->getPosition().getStart().inSeconds();
            const double end   = clip->getPosition().getEnd().inSeconds();
            if (playheadSecs > start + 0.05 && playheadSecs < end - 0.05)
            {
                clipEditor.cutClip (*wc, playheadSecs);
                didSplit = true;
            }
        };

        if (! selectedClips.isEmpty())
        {
            for (auto* cc : selectedClips)
                splitWave (&cc->getClip());
        }
        else
        {
            // Nothing selected: split every clip on every track at playhead
            for (auto* t : te::getAudioTracks (*edit))
                for (auto* c : t->getClips())
                    splitWave (c);
        }

        if (didSplit)
            rebuildTrackRows();
        return true;
    }

    return false;
}

//==============================================================================
// Crossfade

void ArrangementView::applyCrossfadeToSelectedClips()
{
    if (selectedClips.size() < 2)
        return;

    // Collect only WaveAudioClips from the selection
    juce::Array<te::WaveAudioClip*> waveClips;
    for (auto* cc : selectedClips)
        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&cc->getClip()))
            waveClips.add (wc);

    if (waveClips.size() < 2)
        return;

    // Check every pair of wave clips on the same track for an overlap
    bool foundCrossfade = false;
    for (int i = 0; i < waveClips.size() - 1 && ! foundCrossfade; ++i)
    {
        for (int j = i + 1; j < waveClips.size() && ! foundCrossfade; ++j)
        {
            auto* clipA = waveClips[i];
            auto* clipB = waveClips[j];

            // Must be on the same track
            if (clipA->getClipTrack() != clipB->getClipTrack())
                continue;

            const double aStart = clipA->getPosition().getStart().inSeconds();
            const double aEnd   = clipA->getPosition().getEnd().inSeconds();
            const double bStart = clipB->getPosition().getStart().inSeconds();
            const double bEnd   = clipB->getPosition().getEnd().inSeconds();

            const double overlapStart = juce::jmax (aStart, bStart);
            const double overlapEnd   = juce::jmin (aEnd,   bEnd);

            if (overlapEnd <= overlapStart)
                continue;   // no overlap — skip this pair

            const double overlapSecs = overlapEnd - overlapStart;

            // Determine which clip is earlier (starts first = fade-out; later = fade-in)
            te::WaveAudioClip* earlier = (aStart <= bStart) ? clipA : clipB;
            te::WaveAudioClip* later   = (aStart <= bStart) ? clipB : clipA;

            auto& undoMgr = earlier->edit.getUndoManager();
            undoMgr.beginNewTransaction ("Crossfade");

            // Fade OUT on the earlier clip — Equal Power curve (type 1)
            earlier->state.setProperty (te::IDs::fadeOut,     overlapSecs, &undoMgr);
            earlier->state.setProperty (te::IDs::fadeOutType, 1,           &undoMgr);

            // Fade IN on the later clip — Equal Power curve (type 1)
            later->state.setProperty (te::IDs::fadeIn,     overlapSecs, &undoMgr);
            later->state.setProperty (te::IDs::fadeInType, 1,           &undoMgr);

            foundCrossfade = true;
        }
    }

    // Force immediate repaint so fade lines appear without waiting for a timer tick
    if (foundCrossfade)
    {
        for (auto* row : trackRows)
            row->getClipArea().repaint();
        repaint();
    }
}

//==============================================================================
// Tool-dispatching mouse handlers

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    // Right-click: show context menu for empty arrangement space.
    // (Right-click on a ClipComponent is handled by ClipComponent itself.)
    if (e.mods.isPopupMenu())
    {
        auto* edit = appState.editManager.getEdit();
        if (edit == nullptr) return;

        const int xInTimeline   = (e.x - kHeaderWidth) + timelineViewport.getViewPositionX();
        const double clickTime  = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);
        const int    clickY     = e.y;

        juce::PopupMenu menu;
        menu.addItem (1, "Add Audio Track");
        menu.addItem (2, "Add MIDI Track");
        menu.addSeparator();
        menu.addItem (3, "Paste Clip Here",
                      clipEditor.hasClipboardContents());

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this, edit, clickTime, clickY] (int result)
            {
                auto existingTracks = te::getAudioTracks (*edit);
                te::AudioTrack* lastTrack = existingTracks.isEmpty() ? nullptr : existingTracks.getLast();

                if (result == 1)
                {
                    appState.editManager.getUndoManager().beginNewTransaction ("Add Audio Track");
                    auto t = edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, lastTrack), nullptr);
                    if (t != nullptr)
                    {
                        t->setSoloIsolate (false);
                        t->setName (t->getName());  // bake default name into state
                    }
                }
                else if (result == 2)
                {
                    appState.editManager.getUndoManager().beginNewTransaction ("Add MIDI Track");
                    auto t = edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, lastTrack), nullptr);
                    if (t != nullptr)
                    {
                        t->setSoloIsolate (false);
                        t->setName ("MIDI " + juce::String (existingTracks.size() + 1));
                    }
                }
                else if (result == 3)
                {
                    // Paste onto the nearest track below the click Y
                    const int yInContainer = (clickY - kHeaderStripHeight - kRulerHeight)
                                             + timelineViewport.getViewPositionY();
                    const int trackIdx = yContainerToTrackIndex (yInContainer);
                    auto tracks = te::getAudioTracks (*edit);
                    if (trackIdx >= 0 && trackIdx < tracks.size())
                        clipEditor.pasteClip (*tracks[trackIdx], clickTime);
                }
            });
        return;
    }

    if (! e.mods.isLeftButtonDown()) return;

    const int timelineLeft = kHeaderWidth;
    const int timelineTop  = kHeaderStripHeight + kRulerHeight;

    if (e.x < timelineLeft || e.y < timelineTop) return;

    const int xInTimeline = (e.x - timelineLeft) + timelineViewport.getViewPositionX();
    const double clickTimeSecs = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);

    // Command+drag: temporary Marquee (Logic Pro X convention).
    // Handled inline here — NOT via modifierKeysChanged, which also fires on Cmd+scroll (zoom).
    if (e.mods.isCommandDown())
    {
        commandDragMarqueeActive = true;
        rangeActive         = true;
        rangeDragOriginSecs = snapTimeToGrid (clickTimeSecs, false);
        rangeStartSecs      = rangeDragOriginSecs;
        rangeEndSecs        = rangeDragOriginSecs;
        const int yInContainer = (e.y - kHeaderStripHeight - kRulerHeight)
                                 + timelineViewport.getViewPositionY();
        rangeDragTrackIndex = yContainerToTrackIndex (yInContainer);
        rangeTrackStart = rangeTrackEnd = rangeDragTrackIndex;
        grabKeyboardFocus();
        repaint();
        return;
    }

    switch (activeTool)
    {
        case EditTool::Pointer:
        {
            // Store the rubber-band origin FIRST — before any call that may trigger
            // async repaints or ValueTree callbacks — so pointerBandOrigin is always
            // set before mouseDrag fires, even if setPosition has side-effects.
            pointerBandOrigin = e.getPosition();
            pointerBandActive = false;

            // If clicking INSIDE an existing time-range, preserve it so the user can:
            //   draw range with Marquee → switch to Pointer → double-click inside → split.
            // Only seek and deselect when clicking OUTSIDE the current range.
            if (rangeActive && rangeEndSecs > rangeStartSecs
                && clickTimeSecs >= rangeStartSecs && clickTimeSecs <= rangeEndSecs)
                break;

            // Normal pointer click: deselect all clips and seek playhead.
            for (auto* row : trackRows)
                for (int i = 0; i < row->getClipArea().getNumChildComponents(); ++i)
                    if (auto* cc = dynamic_cast<ClipComponent*> (row->getClipArea().getChildComponent (i)))
                        cc->setSelected (false);
            selectedClips.clear();
            if (auto* edit = appState.editManager.getEdit())
                edit->getTransport().setPosition (
                    tracktion::TimePosition::fromSeconds (clickTimeSecs));
            break;
        }

        case EditTool::RangeSelect:
            // Start marquee — existing behaviour, now tool-gated
            selectedClips.clear();
            isMarqueeActive = true;
            marqueeStartPoint = e.getPosition();
            marqueeRect = {};
            break;

        case EditTool::Scissors:
            // Split clip at the click position. ClipComponent::mouseDown also forwards
            // scissors clicks to this method via findParentComponentOfClass, but direct
            // clicks on empty timeline space are handled here.
            splitClipAtTime (clickTimeSecs, e.y);
            break;

        case EditTool::Marquee:
        {
            const double snapped = snapTimeToGrid (clickTimeSecs, e.mods.isAltDown());

            // If clicking INSIDE an existing range, preserve it so mouseDoubleClick
            // can split at both boundaries.  Without this guard the second mouseDown
            // of a double-click sequence resets the range to zero-width before
            // mouseDoubleClick fires, silently aborting the split.
            if (rangeActive && rangeEndSecs > rangeStartSecs
                && snapped >= rangeStartSecs && snapped <= rangeEndSecs)
                break;

            // Start a new time-range selection; drag will extend it.
            rangeActive         = true;
            rangeDragOriginSecs = snapped;
            rangeStartSecs      = rangeDragOriginSecs;
            rangeEndSecs        = rangeDragOriginSecs;
            // Record which track the drag starts on for track-aware selection
            const int yInContainer = (e.y - kHeaderStripHeight - kRulerHeight)
                                     + timelineViewport.getViewPositionY();
            rangeDragTrackIndex = yContainerToTrackIndex (yInContainer);
            rangeTrackStart = rangeTrackEnd = rangeDragTrackIndex;
            break;
        }
    }

    // Reclaim keyboard focus so E/R/S shortcuts work after any left-click in the arrangement.
    grabKeyboardFocus();

    repaint();
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    // Marquee (time-range) drag — track-aware.
    // Covers both the Marquee tool and temporary Marquee (Command+drag / Click Zone).
    if ((activeTool == EditTool::Marquee || commandDragMarqueeActive) && rangeActive)
    {
        const int xInTimeline = (e.x - kHeaderWidth) + timelineViewport.getViewPositionX();
        const double timeSecs = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);
        const double snapped  = snapTimeToGrid (timeSecs, e.mods.isAltDown());
        rangeStartSecs = juce::jmin (rangeDragOriginSecs, snapped);
        rangeEndSecs   = juce::jmax (rangeDragOriginSecs, snapped);

        // Update track range from current Y
        const int yInContainer = (e.y - kHeaderStripHeight - kRulerHeight)
                                 + timelineViewport.getViewPositionY();
        const int currentTrack = yContainerToTrackIndex (yInContainer);
        rangeTrackStart = juce::jmin (rangeDragTrackIndex, currentTrack);
        rangeTrackEnd   = juce::jmax (rangeDragTrackIndex, currentTrack);

        repaint();
        return;
    }

    // ---- Pointer rubber-band (Arrow tool) — drag from empty timeline space ----
    // Activates after a 4px threshold; Cmd+drag is already handled above as a time-range.
    if (activeTool == EditTool::Pointer && !commandDragMarqueeActive)
    {
        const float dx = (float)(e.x - pointerBandOrigin.x);
        const float dy = (float)(e.y - pointerBandOrigin.y);
        if (!pointerBandActive && std::hypot (dx, dy) > 4.0f)
            pointerBandActive = true;

        if (pointerBandActive)
        {
            const int x1 = juce::jmin (pointerBandOrigin.x, e.x);
            const int y1 = juce::jmin (pointerBandOrigin.y, e.y);
            const int x2 = juce::jmax (pointerBandOrigin.x, e.x);
            const int y2 = juce::jmax (pointerBandOrigin.y, e.y);
            pointerBandRect = { x1, y1, x2 - x1, y2 - y1 };

            // Convert to timelineContainer coordinates to test clip intersection
            const int timelineLeft = kHeaderWidth;
            const int timelineTop  = kHeaderStripHeight + kRulerHeight;
            juce::Rectangle<int> containerBand (
                pointerBandRect.getX() - timelineLeft + timelineViewport.getViewPositionX(),
                pointerBandRect.getY() - timelineTop  + timelineViewport.getViewPositionY(),
                pointerBandRect.getWidth(),
                pointerBandRect.getHeight());

            selectedClips.clear();
            for (auto* row : trackRows)
            {
                auto& clipArea = row->getClipArea();
                for (int i = 0; i < clipArea.getNumChildComponents(); ++i)
                {
                    if (auto* cc = dynamic_cast<ClipComponent*> (clipArea.getChildComponent (i)))
                    {
                        const auto clipBounds = cc->getBounds().translated (
                            clipArea.getX() + row->getX(),
                            clipArea.getY() + row->getY());
                        const bool inside = containerBand.intersects (clipBounds);
                        cc->setSelected (inside);
                        if (inside) selectedClips.add (cc);
                    }
                }
            }
            repaint();
        }
        return;
    }

    if (! isMarqueeActive || activeTool != EditTool::RangeSelect) return;

    // Build rectangle from start point to current position
    const int x1 = juce::jmin (marqueeStartPoint.x, e.x);
    const int y1 = juce::jmin (marqueeStartPoint.y, e.y);
    const int x2 = juce::jmax (marqueeStartPoint.x, e.x);
    const int y2 = juce::jmax (marqueeStartPoint.y, e.y);
    marqueeRect = { x1, y1, x2 - x1, y2 - y1 };

    // Only consider it a marquee (not a click) if dragged at least 4px
    if (marqueeRect.getWidth() < 4 && marqueeRect.getHeight() < 4)
        return;

    // Determine which clips intersect the marquee.
    // Convert marquee rect to timeline-container-local coordinates.
    const int timelineLeft = kHeaderWidth;
    const int timelineTop  = kHeaderStripHeight + kRulerHeight;

    juce::Rectangle<int> timelineMarquee (
        marqueeRect.getX() - timelineLeft + timelineViewport.getViewPositionX(),
        marqueeRect.getY() - timelineTop  + timelineViewport.getViewPositionY(),
        marqueeRect.getWidth(),
        marqueeRect.getHeight()
    );

    selectedClips.clear();
    for (auto* row : trackRows)
    {
        auto& clipArea = row->getClipArea();
        for (int i = 0; i < clipArea.getNumChildComponents(); ++i)
        {
            if (auto* cc = dynamic_cast<ClipComponent*> (clipArea.getChildComponent (i)))
            {
                // Convert clip bounds to timeline container coordinates
                auto clipBoundsInContainer = cc->getBounds().translated (
                    clipArea.getX() + row->getX(),
                    clipArea.getY() + row->getY());

                if (timelineMarquee.intersects (clipBoundsInContainer))
                    selectedClips.add (cc);
            }
        }
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    commandDragMarqueeActive = false;  // reset temporary Marquee (Command+drag or Click Zone)

    // Clear pointer rubber-band — selection was already updated live during drag
    if (pointerBandActive)
    {
        pointerBandActive = false;
        pointerBandRect   = {};
        repaint();
    }

    if (isMarqueeActive)
    {
        isMarqueeActive = false;
        // Keep selectedClips — they remain selected after mouse release.
        // The marquee rectangle disappears but selection persists until next click.
        marqueeRect = {};
        repaint();
    }
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click within an active time range: split clip at both boundaries.
    // Studio One workflow: select a region → double-click → Delete to remove it.
    if (!rangeActive || rangeEndSecs <= rangeStartSecs)
        return;

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    juce::ignoreUnused (e);

    auto allTracks = te::getAudioTracks (*edit);

    appState.editManager.getUndoManager().beginNewTransaction ("Split at Range Boundaries");

    // Collect all wave clips that overlap the range on the selected track rows.
    juce::Array<te::WaveAudioClip*> clipsToSplit;
    for (int ti = rangeTrackStart; ti <= rangeTrackEnd && ti < allTracks.size(); ++ti)
    {
        auto* track = allTracks[ti];
        for (auto* clip : track->getClips())
        {
            auto* wc = dynamic_cast<te::WaveAudioClip*> (clip);
            if (wc == nullptr) continue;
            const double start = wc->getPosition().getStart().inSeconds();
            const double end   = wc->getPosition().getEnd().inSeconds();
            // Clip must overlap the range (not just touch it)
            if (end > rangeStartSecs && start < rangeEndSecs)
                clipsToSplit.add (wc);
        }
    }

    // Split at right boundary first so clip positions don't shift before the left split.
    for (auto* wc : clipsToSplit)
    {
        const double clipStart = wc->getPosition().getStart().inSeconds();
        const double clipEnd   = wc->getPosition().getEnd().inSeconds();
        if (rangeEndSecs > clipStart && rangeEndSecs < clipEnd)
            clipEditor.cutClip (*wc, rangeEndSecs);
    }

    // Re-fetch clips — the right-boundary split created new clips; old pointers may be stale.
    clipsToSplit.clear();
    for (int ti = rangeTrackStart; ti <= rangeTrackEnd && ti < allTracks.size(); ++ti)
    {
        auto* track = allTracks[ti];
        for (auto* clip : track->getClips())
        {
            auto* wc = dynamic_cast<te::WaveAudioClip*> (clip);
            if (wc == nullptr) continue;
            const double start = wc->getPosition().getStart().inSeconds();
            const double end   = wc->getPosition().getEnd().inSeconds();
            if (end > rangeStartSecs && start < rangeEndSecs)
                clipsToSplit.add (wc);
        }
    }

    // Split at left boundary.
    for (auto* wc : clipsToSplit)
    {
        const double clipStart = wc->getPosition().getStart().inSeconds();
        const double clipEnd   = wc->getPosition().getEnd().inSeconds();
        if (rangeStartSecs > clipStart && rangeStartSecs < clipEnd)
            clipEditor.cutClip (*wc, rangeStartSecs);
    }

    // Rebuild the track rows to show the new clip boundaries.
    // After rebuild, auto-select the middle segment (the isolated region) so the
    // user can immediately press Delete — Studio One workflow:
    //   Range → double-click → isolated segment selected → Delete removes it.
    const double rStart = rangeStartSecs;  // capture before potential mutation
    const double rEnd   = rangeEndSecs;
    juce::MessageManager::callAsync ([this, rStart, rEnd]
    {
        rebuildTrackRows();
        // Select all clips whose time span exactly matches the split range.
        // These are the newly isolated middle segments from the double-click split.
        selectedClips.clear();
        for (auto* row : trackRows)
        {
            auto& clipArea = row->getClipArea();
            for (int i = 0; i < clipArea.getNumChildComponents(); ++i)
            {
                if (auto* cc = dynamic_cast<ClipComponent*> (clipArea.getChildComponent (i)))
                {
                    const double s = cc->getClip().getPosition().getStart().inSeconds();
                    const double e2 = cc->getClip().getPosition().getEnd().inSeconds();
                    // Tolerance: TE may shift clip boundaries slightly due to snap/sample rounding
                    if (std::abs (s - rStart) < 0.005 && std::abs (e2 - rEnd) < 0.005)
                    {
                        cc->setSelected (true);
                        selectedClips.add (cc);
                    }
                }
            }
        }
        repaint();
    });
}

juce::MouseCursor ArrangementView::getMouseCursor()
{
    switch (activeTool)
    {
        case EditTool::Pointer:
            return juce::MouseCursor (juce::MouseCursor::NormalCursor);
        case EditTool::RangeSelect:
            return juce::MouseCursor (juce::MouseCursor::CrosshairCursor);
        case EditTool::Scissors:
            // No dedicated scissors cursor in JUCE — crosshair is universally
            // understood as a precision tool in DAW conventions.
            return juce::MouseCursor (juce::MouseCursor::CrosshairCursor);
        case EditTool::Marquee:
            return juce::MouseCursor (juce::MouseCursor::IBeamCursor);
    }
    return juce::MouseCursor (juce::MouseCursor::NormalCursor);
}

void ArrangementView::setActiveTool (EditTool tool)
{
    activeTool = tool;
    // Refresh cursor immediately so the change is visible without moving the mouse
    updateMouseCursor();
    // Update toolbar highlight so it reflects the new tool regardless of how the
    // tool was changed (keyboard shortcut, right-click menu, or toolbar click)
    toolSelector.repaint();
}

void ArrangementView::splitClipAtTime (double timeSecs, int screenY)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    // Map screenY → row within the timeline area
    const int timelineTop = kHeaderStripHeight + kRulerHeight;
    int rowLocalY = screenY - timelineTop + timelineViewport.getViewPositionY();

    int cumY = 0;
    for (int ri = 0; ri < trackRows.size(); ++ri)
    {
        auto* row = trackRows[ri];
        const int rowH = (int)(row->getPreferredHeight() * trackHeightScale);

        if (rowLocalY >= cumY && rowLocalY < cumY + rowH)
        {
            auto* track = dynamic_cast<te::AudioTrack*> (
                te::getAudioTracks (*edit)[ri]);
            if (track == nullptr) break;

            // Find the clip that spans timeSecs
            for (auto* clip : track->getClips())
            {
                const double start = clip->getPosition().getStart().inSeconds();
                const double end   = clip->getPosition().getEnd().inSeconds();

                if (timeSecs <= start || timeSecs >= end)
                    continue;   // click is outside this clip

                if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
                {
                    // Delegate to ClipEditor which correctly uses setPosition()
                    // instead of setEnd(preserveSync=false) — the latter silently
                    // corrupts the left clip's source offset (TE subtracts the
                    // trim delta from offset when preserveSync is false).
                    clipEditor.cutClip (*waveClip, timeSecs);
                }
                break;  // only split one clip per click
            }

            // Rebuild so the new clip appears immediately
            juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
            break;
        }
        cumY += rowH;
    }
}

//==============================================================================

void ArrangementView::rebuildTrackRows()
{
    // Clear the inter-track drag guard so future ValueTree changes trigger rebuilds normally.
    isInterTrackDragActive = false;

    // Capture expansion state before destroying TrackRows so open automation
    // lanes survive structural rebuilds (e.g. clip split, track reorder).
    std::set<tracktion::engine::EditItemID> expandedTrackIDs;
    for (auto* row : trackRows)
    {
        if (row->isExpanded())
            expandedTrackIDs.insert (row->getTrack().itemID);
    }

    // Clear existing children
    headerContainer.removeAllChildren();
    timelineContainer.removeAllChildren();
    trackHeaders.clear();
    trackRows.clear();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
    {
        resized();
        return;
    }

    for (auto* audioTrack : te::getAudioTracks (*edit))
    {
        // Bus and FX channels are mixer-only — skip them in the arrangement
        if (! MixerEngine::isAudioChannel (*audioTrack))
            continue;

        // Track header (left panel) — passes plugin management refs for right-click menu
        auto* header = trackHeaders.add (new TrackHeader (*audioTrack,
                                                          mixerEngine,
                                                          automationEngine,
                                                          pluginManager,
                                                          pluginWindowManager,
                                                          pluginScanner));
        headerContainer.addAndMakeVisible (header);

        // Wire selection callback — left-click on header notifies ArrangementView
        header->onSelectionRequest = [this] (TrackHeader* h) {
            setSelectedTrackHeader (h);
        };

        // TrackRow (right panel / timeline) — expandable, holds clips + AutomationLane
        auto* row = trackRows.add (new TrackRow (*audioTrack,
                                                  automationEngine,
                                                  pixelsPerSecond,
                                                  scrollPositionSecs));
        timelineContainer.addAndMakeVisible (row);

        // Wire expansion callback — fires resized() so both header + timeline re-layout
        row->onExpansionChanged = [this] { resized(); };

        // Create clip components inside the row's clip area
        for (auto* clip : audioTrack->getClips())
        {
            auto* cc = new ClipComponent (*clip, appState.editManager, clipEditor, pixelsPerSecond, activeTool);
            row->getClipArea().addAndMakeVisible (cc);

            // Wire single-click selection — notifies ArrangementView so Cmd+C/V works.
            // When the clicked clip is already selected within a multi-selection (e.g. after
            // Cmd+A), preserve the group so the user can start a group drag immediately.
            cc->onSelectRequest = [this] (ClipComponent* clicked)
            {
                if (clicked->isSelected() && selectedClips.size() > 1)
                {
                    // Keep the group selected; drag will start on the whole selection.
                    grabKeyboardFocus();
                    return;
                }

                // Deselect all clips, then select the clicked one
                for (auto* row : trackRows)
                    for (int i = 0; i < row->getClipArea().getNumChildComponents(); ++i)
                        if (auto* other = dynamic_cast<ClipComponent*> (row->getClipArea().getChildComponent (i)))
                            other->setSelected (false);

                selectedClips.clear();
                clicked->setSelected (true);
                selectedClips.add (clicked);
                grabKeyboardFocus();
            };

            // Wire Cmd+click: toggle this clip in/out of the current selection without
            // clearing other selected clips. Enables selecting exactly 2 clips for crossfade.
            cc->onCmdSelectRequest = [this] (ClipComponent* clicked)
            {
                if (clicked->isSelected())
                {
                    clicked->setSelected (false);
                    selectedClips.removeAllInstancesOf (clicked);
                }
                else
                {
                    clicked->setSelected (true);
                    selectedClips.add (clicked);
                }
                grabKeyboardFocus();
            };

            // Wire inter-track drag: reparents the ClipComponent in-place so the drag
            // continues uninterrupted.  rebuildTrackRows() is deferred to mouseUp.
            cc->onInterTrackDrag = [this] (te::Clip& c, double startSecs, int absY,
                                           ClipComponent* comp)
            {
                moveClipByTrackDelta (c, startSecs, absY, comp);
            };
            cc->onDragEnded = [this]
            {
                // Deferred rebuild — runs after mouseUp returns so the component
                // remains alive for the rest of the current event delivery.
                juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
            };

            // Snapshot initial positions of ALL selected clips when a drag begins.
            // Stored in multiDragClips / multiDragInitialTimes for onMultiClipDrag.
            cc->onDragWillStart = [this] (ClipComponent* /*comp*/)
            {
                multiDragClips.clear();
                multiDragInitialTimes.clear();
                for (auto* sel : selectedClips)
                {
                    multiDragClips.add (sel);
                    multiDragInitialTimes.add (sel->getClip().getPosition().getStart().inSeconds());
                }
            };

            // Move all OTHER selected clips by the same snapped delta as the primary.
            cc->onMultiClipDrag = [this] (te::Clip& primaryClip, double deltaSecs)
            {
                for (int i = 0; i < multiDragClips.size(); ++i)
                {
                    auto* sel = multiDragClips[i];
                    if (sel == nullptr || &sel->getClip() == &primaryClip)
                        continue;
                    const double newStart = juce::jmax (0.0, multiDragInitialTimes[i] + deltaSecs);
                    clipEditor.moveClip (sel->getClip(), newStart);
                    const double endSecs = newStart + sel->getClip().getPosition().getLength().inSeconds();
                    sel->setBounds ((int)(newStart * pixelsPerSecond), sel->getY(),
                                    juce::jmax (4, (int)((endSecs - newStart) * pixelsPerSecond)),
                                    sel->getHeight());
                }
            };

            // Position clip within the clip area
            const double startSecs = clip->getPosition().getStart().inSeconds();
            const double endSecs   = clip->getPosition().getEnd().inSeconds();
            const int    x = (int)(startSecs * pixelsPerSecond);
            const int    w = juce::jmax (4, (int)((endSecs - startSecs) * pixelsPerSecond));
            // Clip height is set correctly by layoutClips() inside resized() below;
            // this initial placement just establishes horizontal position.
            cc->setBounds (x, 0, w, TrackRow::kCollapsedHeight);
        }
    }

    // Restore expansion state for tracks that were open before the rebuild
    for (auto* row : trackRows)
    {
        if (expandedTrackIDs.count (row->getTrack().itemID) > 0)
            row->setExpanded (true);
    }

    // Trigger layout
    resized();
}

void ArrangementView::layoutTrackRows()
{
    int y = 0;
    const int timelineW = timelineContainer.getWidth() > 0
                              ? timelineContainer.getWidth()
                              : juce::jmax ((int)(pixelsPerSecond * 300.0),
                                            timelineViewport.getWidth());

    for (auto* row : trackRows)
    {
        const int h = (int)(row->getPreferredHeight() * trackHeightScale);
        row->setBounds (0, y, timelineW, h);
        row->layoutClips (pixelsPerSecond);   // reposition clips at new zoom level
        y += h;
    }
}

//==============================================================================
// ValueTree::Listener — rebuild when the Edit structure changes

void ArrangementView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    // Only rebuild on structural changes: tracks added/removed at the top level,
    // or clips added to a track.  Automation point changes (deeply nested children)
    // must NOT trigger a rebuild — that destroys expanded TrackRows and closes
    // any open automation lanes (gap-42).
    const auto parentType = parent.getType().toString();
    const auto childType  = child.getType().toString();

    // Track-level structural changes: child is a track, or parent is the edit root
    const bool isTrackChange = parent == edit->state
                            || childType  == "TRACK"
                            || childType  == "AUDIOTRACK"
                            || parentType == "TRACKS";

    // Clip-level changes: a clip was added to a track
    const bool isClipChange = childType == "AUDIOCLIP"
                           || childType == "MIDICLIP"
                           || childType == "CLIP";

    // Suppress mid-drag rebuilds: clip.moveTo() in moveClipByTrackDelta fires
    // valueTreeChildAdded/Removed between mouseDrag events.  Rebuilding mid-drag
    // destroys the live ClipComponent and snaps the clip to the wrong position.
    // isInterTrackDragActive is cleared at the start of rebuildTrackRows() (called
    // from onDragEnded on mouseUp) so the rebuild still happens after the drag ends.
    if ((isTrackChange || isClipChange) && !isInterTrackDragActive)
        juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });

    juce::ignoreUnused (parent, child);
}

void ArrangementView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    const auto parentType = parent.getType().toString();
    const auto childType  = child.getType().toString();

    const bool isTrackChange = parent == edit->state
                            || childType  == "TRACK"
                            || childType  == "AUDIOTRACK"
                            || parentType == "TRACKS";

    const bool isClipChange = childType == "AUDIOCLIP"
                           || childType == "MIDICLIP"
                           || childType == "CLIP";

    if ((isTrackChange || isClipChange) && !isInterTrackDragActive)
        juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });

    juce::ignoreUnused (parent, child);
}

void ArrangementView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
        {
            juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
        }
    }
    juce::ignoreUnused (parent);
}

void ArrangementView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // Clip/track property changes (name, position, BPM) may require a repaint.
    // gridCombo.onChange() recomputes gridResolution from the current BPM so the ruler
    // grid lines stay correct after a tempo change (ruler reads gridResolution by ref).
    juce::MessageManager::callAsync ([this] {
        resized();
        repaint();
        rulerStrip.repaint();
        if (gridCombo.onChange)
            gridCombo.onChange();  // recompute gridResolution using live BPM
    });
}

//==============================================================================
// ScrollBar::Listener — keeps header and timeline vertical scroll in sync
// and updates scrollPositionSecs when timeline scrolls horizontally

void ArrangementView::scrollBarMoved (juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &timelineViewport.getVerticalScrollBar())
    {
        headerViewport.setViewPosition (0, (int)newRangeStart);
    }
    else if (scrollBar == &headerViewport.getVerticalScrollBar())
    {
        timelineViewport.setViewPosition (timelineViewport.getViewPositionX(),
                                          (int)newRangeStart);
    }
    else if (scrollBar == &timelineViewport.getHorizontalScrollBar())
    {
        // Keep scrollPositionSecs in sync so ruler and playhead stay aligned
        scrollPositionSecs = newRangeStart / pixelsPerSecond;
        rulerStrip.repaint();
        repaint();  // repaint playhead line with updated scroll offset

        // Reposition automation lane controls so they stay at the visible left edge
        for (auto* row : trackRows)
            row->updateScrollOffset();
    }
}

//==============================================================================
// FileDragAndDropTarget — WAV/AIFF/MP3 import by drag-and-drop

bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    static const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg" };
    for (const auto& filePath : files)
    {
        juce::File f (filePath);
        if (audioExtensions.contains (f.getFileExtension().toLowerCase()))
            return true;
    }
    return false;
}

void ArrangementView::fileDragEnter (const juce::StringArray& /*files*/, int /*x*/, int /*y*/)
{
    isDragHovering = true;
    repaint();
}

void ArrangementView::fileDragExit (const juce::StringArray& /*files*/)
{
    isDragHovering = false;
    repaint();
}

void ArrangementView::filesDropped (const juce::StringArray& files, int dropX, int dropY)
{
    isDragHovering = false;
    repaint();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    auto tracks = te::getAudioTracks (*edit);

    // Convert drop x → timeline time:
    // dropX is in ArrangementView coordinates; subtract header width to get
    // position relative to the timeline viewport's visible area, then add
    // the viewport's horizontal scroll offset to get absolute timeline pixels.
    const int timelineLocalX = dropX - kHeaderWidth;
    const int absoluteTimelineX = timelineLocalX + timelineViewport.getViewPositionX();
    double dropTimeSecs = juce::jmax (0.0, (double)absoluteTimelineX / pixelsPerSecond);

    // Determine which track row the drop landed on.
    // dropY is relative to ArrangementView; subtract the header strip height and
    // ruler strip height to get the y within the track-rows area.
    // Walk track rows to find which row the drop Y falls into, respecting trackHeightScale.
    int rowY      = dropY - kHeaderStripHeight - kRulerHeight;
    int trackIndex = 0;
    {
        int cumY = 0;
        for (int ri = 0; ri < trackRows.size(); ++ri)
        {
            const int rh = (int)(trackRows[ri]->getPreferredHeight() * trackHeightScale);
            if (rowY < cumY + rh) { trackIndex = ri; break; }
            cumY += rh;
            trackIndex = ri;
        }
        trackIndex = juce::jlimit (0, juce::jmax (0, tracks.size() - 1), trackIndex);
    }

    // If no tracks exist, auto-create one before importing
    if (tracks.isEmpty())
    {
        auto& undoManager = appState.editManager.getUndoManager();
        undoManager.beginNewTransaction ("Add Audio Track for Import");
        te::AudioTrack::Ptr firstTrack = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
        if (firstTrack != nullptr)
            firstTrack->setName (firstTrack->getName());  // bake default name into state
        tracks = te::getAudioTracks (*edit);
        if (tracks.isEmpty()) return;
        trackIndex = 0;
    }

    // Import audio files.
    // Multiple files dropped at once: each gets its own track (Studio One / Logic Pro X
    // behaviour — one stem per track, ready for parallel mixing).
    // Single file dropped: lands on the target track at the drop position.
    static const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg" };

    // Collect valid audio files from the drop
    juce::Array<juce::File> audioFiles;
    for (const auto& filePath : files)
    {
        juce::File f (filePath);
        if (audioExtensions.contains (f.getFileExtension().toLowerCase()))
            audioFiles.add (f);
    }

    if (audioFiles.isEmpty())
        return;

    auto& undoManager = appState.editManager.getUndoManager();
    undoManager.beginNewTransaction ("Import Audio Files");

    if (audioFiles.size() == 1)
    {
        // Single file: import onto the target track (existing behaviour)
        clipEditor.importAudioFile (audioFiles[0], *tracks[trackIndex], dropTimeSecs);
    }
    else
    {
        // Multiple files: create one track per file (Logic / Studio One behaviour).
        // The first file lands on the target track; subsequent files each get a
        // new track inserted after the previous one.
        te::AudioTrack* currentTrack = tracks[trackIndex];
        for (int i = 0; i < audioFiles.size(); ++i)
        {
            if (i > 0)
            {
                // Insert a new audio track for each additional file
                currentTrack = edit->insertNewAudioTrack (
                    te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
                if (currentTrack == nullptr) break;
                // Bake the filename-derived name immediately so reorders don't change it
                currentTrack->setName (audioFiles[i].getFileNameWithoutExtension());
                currentTrack->setName (audioFiles[i].getFileNameWithoutExtension());
            }
            clipEditor.importAudioFile (audioFiles[i], *currentTrack, dropTimeSecs);
        }
    }
}

//==============================================================================
// Track selection

void ArrangementView::setSelectedTrackHeader (TrackHeader* header)
{
    if (selectedTrackHeader != nullptr && selectedTrackHeader != header)
        selectedTrackHeader->setSelected (false);

    selectedTrackHeader = header;

    if (header != nullptr)
        header->setSelected (true);

    // Reclaim keyboard focus so S/M/R shortcuts work after TrackHeader clicks.
    // TrackHeader mouseDown never bubbles to ArrangementView::mouseDown(), so focus
    // must be grabbed here explicitly — same pattern as MixerView::setMixerSelectedTrack().
    grabKeyboardFocus();
}

//==============================================================================
// DragAndDropTarget — track header drag-to-reorder

bool ArrangementView::isInterestedInDragSource (
    const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description == "track-reorder";
}

void ArrangementView::itemDragMove (
    const juce::DragAndDropTarget::SourceDetails& details)
{
    // Calculate which row the drag is over for the drop indicator line.
    // Walk scaled row heights so the indicator stays accurate at all zoom levels.
    const int y = details.localPosition.y - kHeaderStripHeight - kRulerHeight;
    {
        int cumY = 0, idx = 0;
        for (int ri = 0; ri < trackRows.size(); ++ri)
        {
            const int rh = (int)(trackRows[ri]->getPreferredHeight() * trackHeightScale);
            if (y < cumY + rh) { idx = ri; break; }
            cumY += rh;
            idx = ri + 1;
        }
        dragOverRowIndex = juce::jlimit (0, trackHeaders.size(), idx);
    }
    repaint();
}

void ArrangementView::itemDragExit (
    const juce::DragAndDropTarget::SourceDetails& /*details*/)
{
    dragOverRowIndex = -1;
    repaint();
}

void ArrangementView::itemDropped (
    const juce::DragAndDropTarget::SourceDetails& details)
{
    dragOverRowIndex = -1;
    repaint();

    auto* sourceHeader = dynamic_cast<TrackHeader*> (details.sourceComponent.get());
    if (sourceHeader == nullptr) return;

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    // Determine drop index from y position, walking scaled row heights.
    const int y = details.localPosition.y - kHeaderStripHeight - kRulerHeight;
    int dropIndex = 0;
    {
        int cumY = 0;
        for (int ri = 0; ri < trackRows.size(); ++ri)
        {
            const int rh = (int)(trackRows[ri]->getPreferredHeight() * trackHeightScale);
            if (y < cumY + rh) { dropIndex = ri; break; }
            cumY += rh;
            dropIndex = ri;
        }
        dropIndex = juce::jlimit (0, juce::jmax (0, trackHeaders.size() - 1), dropIndex);
    }

    // Find current index of the dragged track header
    const int sourceIndex = trackHeaders.indexOf (sourceHeader);
    if (sourceIndex < 0 || sourceIndex == dropIndex) return;

    // Reorder via ValueTree::moveChild (undoable)
    auto& undoManager = appState.editManager.getUndoManager();
    undoManager.beginNewTransaction ("Reorder Track");

    // Build the filtered visible-track list (same filter as rebuildTrackRows) so we
    // can map row indices to actual ValueTree child positions in edit->state.
    // edit->state also contains non-track children (TempoSequence, etc.) so we cannot
    // use the row index directly as the moveChild target index — it must be the real
    // child index of the target track in edit->state.
    juce::Array<te::AudioTrack*> visibleTracks;
    for (auto* t : te::getAudioTracks (*edit))
        if (MixerEngine::isAudioChannel (*t))
            visibleTracks.add (t);

    if (sourceIndex < 0 || sourceIndex >= visibleTracks.size()) return;
    if (dropIndex   < 0 || dropIndex   >= visibleTracks.size()) return;

    // JUCE ValueTree::moveChild (src, dst) places the item at final position dst in
    // the result array (no +1 adjustment needed).  Using the state index of the track
    // currently occupying dropIndex as dst gives the correct result for both up and
    // down moves:
    //   Moving UP   (dropIndex < sourceIndex): item lands before visibleTracks[dropIndex] ✓
    //   Moving DOWN (dropIndex > sourceIndex): item lands at visibleTracks[dropIndex]'s
    //       old position, pushing that track one step forward ✓
    const int sourceChildIndex = edit->state.indexOf (visibleTracks[sourceIndex]->state);
    const int targetChildIndex = edit->state.indexOf (visibleTracks[dropIndex]->state);
    if (sourceChildIndex < 0 || targetChildIndex < 0) return;

    edit->state.moveChild (sourceChildIndex, targetChildIndex, &undoManager);
    // valueTreeChildOrderChanged listener will call rebuildTrackRows() asynchronously
}

int ArrangementView::yContainerToTrackIndex (int yInContainer) const
{
    int cumY = 0;
    for (int i = 0; i < trackRows.size(); ++i)
    {
        const int h = (int)(trackRows[i]->getPreferredHeight() * trackHeightScale);
        if (yInContainer < cumY + h)
            return i;
        cumY += h;
    }
    return juce::jmax (0, trackRows.size() - 1);  // clamp to last row
}

void ArrangementView::deselectAllClips()
{
    for (auto* row : trackRows)
        for (int i = 0; i < row->getClipArea().getNumChildComponents(); ++i)
            if (auto* cc = dynamic_cast<ClipComponent*> (row->getClipArea().getChildComponent (i)))
                cc->setSelected (false);
    selectedClips.clear();
    repaint();
}

void ArrangementView::startRangeSelectionFromClickZone (const juce::MouseEvent& e)
{
    // Called by ClipComponent when Pointer tool + mouse in lower half of clip.
    // Starts a temporary time-range selection without switching activeTool.
    commandDragMarqueeActive = true;

    const int timelineLeft = kHeaderWidth;
    const int xInTimeline  = (e.x - timelineLeft) + timelineViewport.getViewPositionX();
    const double clickTimeSecs = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);

    rangeActive         = true;
    rangeDragOriginSecs = snapTimeToGrid (clickTimeSecs, e.mods.isAltDown());
    rangeStartSecs      = rangeDragOriginSecs;
    rangeEndSecs        = rangeDragOriginSecs;

    const int yInContainer = (e.y - kHeaderStripHeight - kRulerHeight)
                             + timelineViewport.getViewPositionY();
    rangeDragTrackIndex = yContainerToTrackIndex (yInContainer);
    rangeTrackStart = rangeTrackEnd = rangeDragTrackIndex;

    repaint();
}

void ArrangementView::moveClipByTrackDelta (te::Clip& clip, double startTimeSecs,
                                             int absoluteYInContainer,
                                             ClipComponent* draggedComp)
{
    // Find which track the clip currently lives on
    int sourceTrackIndex = -1;
    for (int i = 0; i < trackRows.size(); ++i)
    {
        if (&trackRows[i]->getTrack() == clip.getClipTrack())
        {
            sourceTrackIndex = i;
            break;
        }
    }
    if (sourceTrackIndex < 0)
        return;

    // Determine target track from absolute Y in timelineContainer
    const int targetTrackIndex = yContainerToTrackIndex (absoluteYInContainer);

    if (targetTrackIndex == sourceTrackIndex)
        return;

    auto* destTrack = dynamic_cast<te::AudioTrack*> (&trackRows[targetTrackIndex]->getTrack());
    if (destTrack == nullptr)
        return;

    // Suppress ValueTree-triggered rebuildTrackRows() while the drag is live.
    // clip.moveTo() fires valueTreeChildRemoved + valueTreeChildAdded which would
    // otherwise queue callAsync(rebuildTrackRows) and destroy this component mid-drag.
    isInterTrackDragActive = true;

    // Move the TE clip data to the target track at the given position.
    // moveClipToTrack begins its own undo transaction; moveClip appends to it.
    clipEditor.moveClipToTrack (clip, *destTrack);
    clipEditor.moveClip (clip, startTimeSecs);

    // Physically reparent the ClipComponent to the destination track's clip area so
    // the drag continues uninterrupted.  All clip areas share the same screen X origin
    // (each clipArea is at x=0 in its TrackRow; each TrackRow is at x=0 in
    // timelineContainer), so the component's X coordinates remain valid after reparent.
    // rebuildTrackRows() is deferred to mouseUp via ClipComponent::onDragEnded.
    if (draggedComp != nullptr)
    {
        auto& destClipArea = trackRows[targetTrackIndex]->getClipArea();
        // Derive X from startTimeSecs (the clean original position), NOT from
        // draggedComp->getX().  The component's current X was set by setBounds()
        // in mouseDrag using newStartSecs (which included snappedDelta drift),
        // so reading it back would place the clip at the wrong visual position.
        const int savedX = (int)(startTimeSecs * pixelsPerSecond);
        const int savedW = draggedComp->getWidth();
        const int destH  = destClipArea.getHeight();

        draggedComp->getParentComponent()->removeChildComponent (draggedComp);
        destClipArea.addAndMakeVisible (draggedComp);
        draggedComp->setBounds (savedX, 0, savedW, destH);

        // Signal the component to rebuild on mouseUp
        draggedComp->pendingRebuild = true;
    }
}

//==============================================================================
// deleteTimeRange — Marquee tool: delete the selected time region from all clips

void ArrangementView::deleteTimeRange()
{
    if (!rangeActive || rangeEndSecs <= rangeStartSecs)
        return;

    const double rStart = rangeStartSecs;
    const double rEnd   = rangeEndSecs;

    auto& undoManager = appState.editManager.getUndoManager();
    undoManager.beginNewTransaction ("Delete Time Range");

    // Collect clips only from the tracks covered by the Marquee selection
    juce::Array<te::Clip*> allClips;
    for (int ri = rangeTrackStart; ri <= rangeTrackEnd && ri < trackRows.size(); ++ri)
        for (int i = 0; i < trackRows[ri]->getClipArea().getNumChildComponents(); ++i)
            if (auto* cc = dynamic_cast<ClipComponent*> (trackRows[ri]->getClipArea().getChildComponent (i)))
                allClips.add (&cc->getClip());

    for (auto* clip : allClips)
    {
        const double cStart = clip->getPosition().getStart().inSeconds();
        const double cEnd   = clip->getPosition().getEnd().inSeconds();

        if (cEnd <= rStart || cStart >= rEnd)
            continue;  // no overlap

        if (cStart >= rStart && cEnd <= rEnd)
        {
            // Clip entirely inside range: delete it
            clipEditor.deleteClip (*clip);
        }
        else if (cStart < rStart && cEnd > rEnd)
        {
            // Range is a hole inside the clip: cut at rStart, then trim the right piece
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
            {
                // After cutClip: original clip = [cStart, rStart]; newClip = [rStart, cEnd]
                if (auto* rightPiece = clipEditor.cutClip (*wave, rStart))
                {
                    // Trim the right piece so it starts at rEnd (skip the deleted region)
                    clipEditor.trimClip (*rightPiece, rEnd, cEnd);
                }
            }
        }
        else if (cStart < rStart)
        {
            // Clip overlaps range on the right: trim clip end to range start
            clipEditor.trimClip (*clip, cStart, rStart);
        }
        else
        {
            // Clip overlaps range on the left: trim clip start to range end
            clipEditor.trimClip (*clip, rEnd, cEnd);
        }
    }

    rangeActive = false;
    juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
    repaint();
}
