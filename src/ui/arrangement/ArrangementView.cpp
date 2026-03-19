#include "ArrangementView.h"

namespace te = tracktion::engine;

//==============================================================================
// RulerStrip

ArrangementView::RulerStrip::RulerStrip (ApplicationState& appState_,
                                          double& pixelsPerSecond_,
                                          double& scrollPositionSecs_)
    : appState          (appState_)
    , pixelsPerSecond   (pixelsPerSecond_)
    , scrollPositionSecs (scrollPositionSecs_)
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
}

void ArrangementView::RulerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    // Convert click x to an absolute timeline time, accounting for scroll offset.
    // scrollPositionSecs * pixelsPerSecond gives the horizontal scroll in pixels;
    // adding e.x yields the absolute pixel position in the timeline.
    const double clickTimeSecs = juce::jmax (
        0.0,
        (e.x + scrollPositionSecs * pixelsPerSecond) / pixelsPerSecond);

    edit->getTransport().setPosition (
        tracktion::TimePosition::fromSeconds (clickTimeSecs));
}

void ArrangementView::RulerStrip::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging in the ruler = scrub: update playhead position continuously.
    // Reuses mouseDown logic — no additional state needed.
    mouseDown (e);
}

juce::MouseCursor ArrangementView::RulerStrip::getMouseCursor()
{
    // Left-right resize cursor signals "this is an interactive scrub target"
    // — matches Logic Pro and Ableton Live conventions.
    return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

//==============================================================================
// ToolbarStrip

ArrangementView::ToolbarStrip::ToolbarStrip()
{
    // Style all three buttons identically — flat, dark, no border
    auto styleBtn = [] (juce::TextButton& btn)
    {
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0x00000000));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x00000000));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffaaaaaa));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xffffffff));
    };
    styleBtn (pointerBtn);
    styleBtn (selectBtn);
    styleBtn (scissorsBtn);

    addAndMakeVisible (pointerBtn);
    addAndMakeVisible (selectBtn);
    addAndMakeVisible (scissorsBtn);

    // Button clicks fire onToolChange so ArrangementView can update activeTool
    pointerBtn.onClick  = [this] { if (onToolChange) onToolChange (EditTool::Pointer);     };
    selectBtn.onClick   = [this] { if (onToolChange) onToolChange (EditTool::RangeSelect); };
    scissorsBtn.onClick = [this] { if (onToolChange) onToolChange (EditTool::Scissors);    };

    updateButtonStates();
}

void ArrangementView::ToolbarStrip::resized()
{
    // Three equal-width buttons filling the strip horizontally
    const int w = getWidth() / 3;
    pointerBtn .setBounds (0,       0, w, getHeight());
    selectBtn  .setBounds (w,       0, w, getHeight());
    scissorsBtn.setBounds (w * 2,   0, getWidth() - w * 2, getHeight());
}

void ArrangementView::ToolbarStrip::paint (juce::Graphics& g)
{
    // Subtle separator on the right edge of the toolbar area
    g.setColour (juce::Colour (0xff333333));
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float)getHeight());
}

void ArrangementView::ToolbarStrip::updateButtonStates()
{
    // Highlight the active tool button with Apple system blue at 20% alpha
    const juce::Colour activeColour  (0x33007aff);  // 0x33 = ~20% alpha, 007aff = Apple blue
    const juce::Colour inactiveColour (0x00000000); // transparent

    pointerBtn .setColour (juce::TextButton::buttonColourId,
                           activeTool == EditTool::Pointer     ? activeColour : inactiveColour);
    selectBtn  .setColour (juce::TextButton::buttonColourId,
                           activeTool == EditTool::RangeSelect ? activeColour : inactiveColour);
    scissorsBtn.setColour (juce::TextButton::buttonColourId,
                           activeTool == EditTool::Scissors    ? activeColour : inactiveColour);

    // Text colour also brightens on the active tool
    const juce::Colour activeText   (0xffffffff);
    const juce::Colour inactiveText (0xffaaaaaa);

    pointerBtn .setColour (juce::TextButton::textColourOffId,
                           activeTool == EditTool::Pointer     ? activeText : inactiveText);
    selectBtn  .setColour (juce::TextButton::textColourOffId,
                           activeTool == EditTool::RangeSelect ? activeText : inactiveText);
    scissorsBtn.setColour (juce::TextButton::textColourOffId,
                           activeTool == EditTool::Scissors    ? activeText : inactiveText);

    repaint();
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
    , rulerStrip          (appState_, pixelsPerSecond, scrollPositionSecs)
{
    // Header viewport — scrolls vertically only (no horizontal scroll)
    headerViewport.setScrollBarsShown (true, false);
    headerViewport.setViewedComponent (&headerContainer, false);
    addAndMakeVisible (headerViewport);

    // Timeline viewport — scrolls both horizontally and vertically
    timelineViewport.setScrollBarsShown (true, true);
    timelineViewport.setViewedComponent (&timelineContainer, false);
    addAndMakeVisible (timelineViewport);

    // Keep vertical scroll positions in sync between header and timeline.
    // juce::ScrollBar does not have onValueChange — must use addListener().
    timelineViewport.getVerticalScrollBar().addListener (this);
    headerViewport.getVerticalScrollBar().addListener (this);

    // Horizontal scroll listener — keeps scrollPositionSecs in sync with ruler
    timelineViewport.getHorizontalScrollBar().addListener (this);

    // Add Track button — header strip, top-left
    addAndMakeVisible (addTrackButton);
    addTrackButton.setTooltip ("Add a new audio track (Cmd+T)");
    addTrackButton.onClick = [this]
    {
        auto* edit = this->appState.editManager.getEdit();
        if (edit == nullptr) return;

        auto& undoManager = this->appState.editManager.getUndoManager();
        undoManager.beginNewTransaction ("Add Audio Track");
        edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr),
                                   nullptr);  // nullptr = no SelectionManager
        // ArrangementView's ValueTree listener (valueTreeChildAdded) will
        // automatically call rebuildTrackRows() — no manual update needed
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

    // Wire ZoomViewport callback — intercepts Ctrl/Cmd+scroll over the timeline and
    // delegates to the existing zoom logic in ArrangementView::mouseWheelMove.
    // e.getEventRelativeTo(this) converts the event coordinates from viewport-local
    // to ArrangementView-local so the zoom-to-cursor calculation works correctly.
    timelineViewport.onZoomWheel = [this] (const juce::MouseEvent& e,
                                            const juce::MouseWheelDetails& w)
    {
        mouseWheelMove (e.getEventRelativeTo (this), w);
    };

    // Toolbar strip — edit tool buttons in the header strip
    addAndMakeVisible (toolbarStrip);
    toolbarStrip.onToolChange = [this] (EditTool tool) { setActiveTool (tool); };

    // Ruler strip — above timeline rows, aligned with timeline panel (not headers)
    addAndMakeVisible (rulerStrip);

    // Keyboard focus needed for left/right arrow scrubbing
    setWantsKeyboardFocus (true);

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

    // Zoom buttons — top-right of the header strip, 20×20 each with 4px gap
    // Place right-to-left: zoomIn (+) rightmost, then zoomOut (−) to its left.
    zoomInButton.setBounds  (getWidth() - 28, headerStrip.getY() + 4, 20, 20);
    zoomOutButton.setBounds (getWidth() - 52, headerStrip.getY() + 4, 20, 20);

    // Toolbar strip — three tool buttons (Pointer/RangeSelect/Scissors), 24px each,
    // placed to the right of the Add Track button and left of the zoom buttons.
    // Centre-aligned vertically in the header strip.
    const int toolBtnW = 24;
    const int toolbarW = toolBtnW * 3;
    const int toolbarX = 84;   // just right of Add Track button (80 + 4px gap)
    toolbarStrip.setBounds (toolbarX, headerStrip.getY(), toolbarW, kHeaderStripHeight);

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
        const int rowH = trackRows[i]->getPreferredHeight();
        trackHeaders[i]->setBounds (0, headerY, kHeaderWidth, rowH);
        headerY += rowH;
    }

    // Update headerContainer height to match total track row height
    const int totalHeaderH = headerY;
    headerContainer.setSize (kHeaderWidth,
                             juce::jmax (totalHeaderH, headerViewport.getHeight()));

    // Lay out TrackRows in the timeline container
    layoutTrackRows();

    // Timeline container height = sum of all TrackRow preferred heights
    int totalRowH = 0;
    for (auto* row : trackRows)
        totalRowH += row->getPreferredHeight();

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
        const int indicatorY = kHeaderStripHeight + kRulerHeight
                             + dragOverRowIndex * kTrackHeight;
        g.setColour (juce::Colour (0xff3d9eff));  // Apple system blue accent
        g.fillRect (0, indicatorY - 1, getWidth(), 2);
    }

    // Apple Final Cut Pro style playhead: thin platinum/white vertical line
    if (appState.editManager.getEdit() != nullptr)
    {
        const double posX = (playheadPositionSecs - scrollPositionSecs) * pixelsPerSecond;
        const int screenX = kHeaderWidth + (int)posX
                            - timelineViewport.getViewPositionX();
        const int rulerBottom = kHeaderStripHeight + kRulerHeight;

        if (screenX >= kHeaderWidth && screenX <= getWidth())
        {
            g.setColour (juce::Colour (0xffd0d0d0).withAlpha (0.85f));
            g.drawVerticalLine (screenX, (float)rulerBottom, (float)getHeight());
        }
    }

    // ---- Marquee selection rectangle ----
    if (isMarqueeActive && ! marqueeRect.isEmpty())
    {
        // Semi-transparent blue fill
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.15f));
        g.fillRect (marqueeRect);

        // Blue border
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.6f));
        g.drawRect (marqueeRect, 1);
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

void ArrangementView::setPixelsPerSecond (double pps)
{
    pixelsPerSecond = juce::jlimit (10.0, 2000.0, pps);
    resized();
    repaint();
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel)
{
    // Pinch-to-zoom: macOS trackpad delivers pinch as Ctrl+scroll-Y
    // Also handle Cmd+scroll for explicit keyboard+scroll zoom
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
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

        // Scroll so the same time position remains under the cursor
        const int newMouseXInTimeline = (int)(timeAtCursor * pixelsPerSecond);
        const int newScrollX = newMouseXInTimeline - (event.getPosition().x - kHeaderWidth);
        timelineViewport.setViewPosition (juce::jmax (0, newScrollX),
                                          timelineViewport.getViewPositionY());
        scrollPositionSecs = juce::jmax (0.0, (double)newScrollX / pixelsPerSecond);

        resized();
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

    const double newPos = edit->getTransport().getPosition().inSeconds();
    if (std::abs (newPos - playheadPositionSecs) > 0.001)
    {
        playheadPositionSecs = newPos;
        // Repaint timeline container and ruler (not headers — avoids layout flicker)
        timelineContainer.repaint();
        rulerStrip.repaint();
        repaint();  // repaints the playhead line drawn in ArrangementView::paint()
    }

    // Auto-follow playhead during playback
    if (edit->getTransport().isPlaying())
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

    // ---- Tool shortcuts (standard DAW: E = Pointer, R = Range Select, S = Scissors) ----
    // These must not require modifiers — plain letter keys only.
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (key.getKeyCode() == 'e' || key.getKeyCode() == 'E')
        {
            setActiveTool (EditTool::Pointer);
            return true;
        }
        if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
        {
            setActiveTool (EditTool::RangeSelect);
            return true;
        }
        if (key.getKeyCode() == 's' || key.getKeyCode() == 'S')
        {
            setActiveTool (EditTool::Scissors);
            return true;
        }
    }

    return false;
}

//==============================================================================
// Tool-dispatching mouse handlers

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;

    const int timelineLeft = kHeaderWidth;
    const int timelineTop  = kHeaderStripHeight + kRulerHeight;

    if (e.x < timelineLeft || e.y < timelineTop) return;

    const int xInTimeline = (e.x - timelineLeft) + timelineViewport.getViewPositionX();
    const double clickTimeSecs = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);

    switch (activeTool)
    {
        case EditTool::Pointer:
            // Seek playhead — existing behaviour. Clip dragging is handled
            // by ClipComponent itself (does not reach this handler).
            selectedClips.clear();
            if (auto* edit = appState.editManager.getEdit())
                edit->getTransport().setPosition (
                    tracktion::TimePosition::fromSeconds (clickTimeSecs));
            break;

        case EditTool::RangeSelect:
            // Start marquee — existing behaviour, now tool-gated
            selectedClips.clear();
            isMarqueeActive = true;
            marqueeStartPoint = e.getPosition();
            marqueeRect = {};
            break;

        case EditTool::Scissors:
            // Split clip at the click position
            splitClipAtTime (clickTimeSecs, e.y);
            break;
    }

    repaint();
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
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
    if (isMarqueeActive)
    {
        isMarqueeActive = false;
        // Keep selectedClips — they remain selected after mouse release.
        // The marquee rectangle disappears but selection persists until next click.
        marqueeRect = {};
        repaint();
    }
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
    }
    return juce::MouseCursor (juce::MouseCursor::NormalCursor);
}

void ArrangementView::setActiveTool (EditTool tool)
{
    activeTool = tool;
    toolbarStrip.activeTool = tool;
    toolbarStrip.updateButtonStates();
    // Refresh cursor immediately so the change is visible without moving the mouse
    updateMouseCursor();
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
        const int rowH = row->getPreferredHeight();

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

                // Attempt to use TE v3.2.0 splitClipAt if available.
                // If the free-function is not available, fall back to duplicate-and-trim.
                if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
                {
                    auto& undoManager = appState.editManager.getUndoManager();
                    undoManager.beginNewTransaction ("Split Clip");

                    // Duplicate-and-trim fallback:
                    // 1. Record original range before trimming the original
                    const double originalEnd   = end;

                    // 2. Trim the original clip's end to the split point
                    waveClip->setEnd (tracktion::TimePosition::fromSeconds (timeSecs), false);

                    // 3. Insert a new clip from the split point to the original end,
                    //    referencing the same source file with the offset shifted.
                    const double offsetShift = timeSecs - start;  // seconds into the source file
                    te::ClipPosition newPos {
                        tracktion::TimeRange (
                            tracktion::TimePosition::fromSeconds (timeSecs),
                            tracktion::TimePosition::fromSeconds (originalEnd)),
                        tracktion::TimeDuration()
                    };

                    auto newClipState = track->insertNewClip (
                        te::TrackItem::Type::wave,
                        "Split",
                        newPos,
                        nullptr);   // no SelectionManager in Phase 1

                    if (auto* newWave = dynamic_cast<te::WaveAudioClip*> (newClipState))
                    {
                        // Point new clip at the same source file
                        newWave->getSourceFileReference().setToDirectFileReference (
                            waveClip->getSourceFileReference().getFile(), true);

                        // Shift the source offset so the new clip plays the correct
                        // portion: original offset + shift into the source.
                        const double originalOffset = waveClip->getPosition().getOffset().inSeconds();
                        newWave->setOffset (
                            tracktion::TimeDuration::fromSeconds (originalOffset + offsetShift));
                    }
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
        // Track header (left panel) — passes plugin management refs for right-click menu
        auto* header = trackHeaders.add (new TrackHeader (*audioTrack,
                                                          mixerEngine,
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
            auto* cc = new ClipComponent (*clip, appState.editManager, clipEditor, pixelsPerSecond);
            row->getClipArea().addAndMakeVisible (cc);

            // Position clip within the clip area
            const double startSecs = clip->getPosition().getStart().inSeconds();
            const double endSecs   = clip->getPosition().getEnd().inSeconds();
            const int    x = (int)((startSecs - scrollPositionSecs) * pixelsPerSecond);
            const int    w = juce::jmax (4, (int)((endSecs - startSecs) * pixelsPerSecond));
            cc->setBounds (x, 2, w, TrackRow::kCollapsedHeight - 4);
        }
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
        const int h = row->getPreferredHeight();
        row->setBounds (0, y, timelineW, h);
        y += h;
    }
}

//==============================================================================
// ValueTree::Listener — rebuild when the Edit structure changes

void ArrangementView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    // Only react to track/clip changes inside the Edit state
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
        {
            juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
        }
    }
    juce::ignoreUnused (parent, child);
}

void ArrangementView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
        {
            juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
        }
    }
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
    // Clip/track property changes (name, position, BPM) may require a repaint
    // rulerStrip.repaint() ensures Gap 9 (BPM change → ruler update) is reactive
    juce::MessageManager::callAsync ([this] {
        resized();
        repaint();
        rulerStrip.repaint();
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
    int rowY = dropY - kHeaderStripHeight - kRulerHeight;
    int trackIndex = juce::jlimit (0, juce::jmax (0, tracks.size() - 1), rowY / kTrackHeight);

    // If no tracks exist, auto-create one before importing
    if (tracks.isEmpty())
    {
        auto& undoManager = appState.editManager.getUndoManager();
        undoManager.beginNewTransaction ("Add Audio Track for Import");
        edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, nullptr), nullptr);
        tracks = te::getAudioTracks (*edit);
        if (tracks.isEmpty()) return;
        trackIndex = 0;
    }

    // Import all audio files in the drop (multi-file drops stack sequentially)
    static const juce::StringArray audioExtensions { ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg" };
    double offsetSecs = 0.0;
    for (const auto& filePath : files)
    {
        juce::File f (filePath);
        if (! audioExtensions.contains (f.getFileExtension().toLowerCase()))
            continue;

        auto* clip = clipEditor.importAudioFile (f, *tracks[trackIndex], dropTimeSecs + offsetSecs);
        if (clip != nullptr)
        {
            // Advance offset so the next file starts after this one
            offsetSecs += clip->getPosition().getLength().inSeconds();
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
    // localPosition is relative to this component; subtract header strip and ruler.
    const int y = details.localPosition.y - kHeaderStripHeight - kRulerHeight;
    dragOverRowIndex = juce::jlimit (0, trackHeaders.size(), y / kTrackHeight);
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

    // Determine drop index from y position
    const int y = details.localPosition.y - kHeaderStripHeight - kRulerHeight;
    const int dropIndex = juce::jlimit (0, trackHeaders.size() - 1, y / kTrackHeight);

    // Find current index of the dragged track header
    const int sourceIndex = trackHeaders.indexOf (sourceHeader);
    if (sourceIndex < 0 || sourceIndex == dropIndex) return;

    // Reorder via ValueTree::moveChild (undoable)
    auto& undoManager = appState.editManager.getUndoManager();
    undoManager.beginNewTransaction ("Reorder Track");

    // TE tracks are direct children of edit->state
    const auto& trackState = sourceHeader->getTrack().state;
    const int currentChildIndex = edit->state.indexOf (trackState);
    if (currentChildIndex < 0) return;

    // Adjust target index: when moving down, +1 compensates for the source removal
    int targetIndex = dropIndex;
    if (dropIndex > sourceIndex) targetIndex++;

    edit->state.moveChild (currentChildIndex, targetIndex, &undoManager);
    // valueTreeChildOrderChanged listener will call rebuildTrackRows() asynchronously
}
