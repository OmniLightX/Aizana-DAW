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

    // Two tool buttons: Pointer (V) and Scissors (S), each 36px wide
    constexpr int kButtonW = 36;
    constexpr int kButtonH = 20;
    const int     stripH   = getHeight();
    const int     yOff     = (stripH - kButtonH) / 2;

    struct ButtonDef { EditTool tool; const char* label; int x; };
    const ButtonDef buttons[2] = {
        { EditTool::Pointer,  "V", 0        },
        { EditTool::Scissors, "S", kButtonW },
    };

    for (const auto& btn : buttons)
    {
        juce::Rectangle<int> r (btn.x, yOff, kButtonW, kButtonH);
        const bool isActive = (activeTool == btn.tool);

        if (isActive)
        {
            // Highlighted: accent fill (Apple blue, 40% opacity)
            g.setColour (juce::Colour (0xff4488ff).withAlpha (0.40f));
            g.fillRoundedRectangle (r.toFloat().reduced (1.0f), 3.0f);
        }

        // Label — bright white when active, dim grey when inactive
        g.setColour (isActive ? juce::Colour (0xffffffff) : juce::Colour (0xff888888));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (btn.label, r, juce::Justification::centred, false);
    }

    // 1px separator between the two buttons
    g.setColour (juce::Colour (0xff333333));
    const int sepX = kButtonW;
    const int sepY0 = yOff + 2;
    const int sepY1 = yOff + kButtonH - 2;
    g.drawVerticalLine (sepX, (float)sepY0, (float)sepY1);
}

void ArrangementView::ToolSelectorStrip::mouseDown (const juce::MouseEvent& e)
{
    // Hit-test x to determine which button was clicked (only Pointer + Scissors)
    constexpr int kButtonW = 36;
    if (e.x < kButtonW)
        onToolSelected (EditTool::Pointer);
    else
        onToolSelected (EditTool::Scissors);
}

//==============================================================================
// Snap-to-grid helper

double ArrangementView::snapTimeToGrid (double timeSecs, bool altDown) const noexcept
{
    if (!snapEnabled || altDown || gridResolution <= 0.0)
        return timeSecs;
    return std::round (timeSecs / gridResolution) * gridResolution;
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

                if (result == 1)
                {
                    // Audio track (existing behaviour)
                    undoManager.beginNewTransaction ("Add Audio Track");
                    edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, nullptr), nullptr);
                    // ArrangementView's ValueTree listener (valueTreeChildAdded) will
                    // automatically call rebuildTrackRows() — no manual update needed
                }
                else if (result == 2)
                {
                    // MIDI track: create an AudioTrack and insert an empty MIDI clip
                    undoManager.beginNewTransaction ("Add MIDI Track");
                    te::AudioTrack::Ptr newTrack = edit->insertNewAudioTrack (
                        te::TrackInsertPoint (nullptr, nullptr), nullptr);

                    if (newTrack != nullptr)
                    {
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

        const int newMouseXInTimeline = (int)(timeAtCursor * pixelsPerSecond);
        const int newScrollX = newMouseXInTimeline - (eventInAV.getPosition().x - kHeaderWidth);
        timelineViewport.setViewPosition (juce::jmax (0, newScrollX),
                                          timelineViewport.getViewPositionY());
        scrollPositionSecs = juce::jmax (0.0, (double)newScrollX / pixelsPerSecond);

        resized();
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

    // Tool selector strip — 76px wide, immediately to the right of the Add Track button
    toolSelector.setBounds (headerStrip.removeFromLeft (76).reduced (2));

    // Zoom buttons — top-right of the header strip, 20×20 each with 4px gap
    // Place right-to-left: zoomIn (+) rightmost, then zoomOut (−) to its left.
    zoomInButton.setBounds  (getWidth() - 28, headerStrip.getY() + 4, 20, 20);
    zoomOutButton.setBounds (getWidth() - 52, headerStrip.getY() + 4, 20, 20);

    // Snap controls — to the right of the tool selector, before the zoom buttons.
    // snapButton (40px) + 4px gap + gridCombo (56px) placed after toolSelector.
    const int snapBtnX = 80 + 4 + 76 + 4;  // addTrackButton(80) + gap(4) + toolSelector(76) + gap(4)
    snapButton.setBounds (snapBtnX, headerStrip.getY() + 4, 40, 20);
    gridCombo.setBounds  (snapBtnX + 44, headerStrip.getY() + 4, 56, 20);

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

    // Apple Final Cut Pro style playhead: thin platinum/white vertical line
    if (appState.editManager.getEdit() != nullptr)
    {
        // Playhead X in absolute timeline pixels
        const int absolutePlayheadX = (int)(playheadPositionSecs * pixelsPerSecond);
        // Convert to screen coordinates: subtract viewport scroll, add header width
        const int screenX = kHeaderWidth + absolutePlayheadX - timelineViewport.getViewPositionX();
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
    // Escape always returns to Pointer — no modifier check needed.
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        setActiveTool (EditTool::Pointer);
        return true;
    }

    // Letter-key shortcuts (plain keys only, no modifiers):
    // V/E = Pointer (Logic Pro convention + backward compat), R = Range Select, S = Scissors.
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (key.getKeyCode() == 'v' || key.getKeyCode() == 'V'
            || key.getKeyCode() == 'e' || key.getKeyCode() == 'E')
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
                    auto tracks = te::getAudioTracks (*currentEdit);
                    if (tracks.size() > 0)
                        targetTrack = tracks[0];
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

    return false;
}

//==============================================================================
// Tool-dispatching mouse handlers

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    // Right-click anywhere in the arrangement shows a context menu for tool selection.
    // Using isPopupMenu() covers right-click AND Ctrl+click (macOS convention).
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Edit Tool");
        menu.addItem (1, juce::String ("Pointer")
                         + (activeTool == EditTool::Pointer     ? "  \xe2\x9c\x93" : "")
                         + "   E");
        menu.addItem (2, juce::String ("Range Select")
                         + (activeTool == EditTool::RangeSelect ? "  \xe2\x9c\x93" : "")
                         + "   R");
        menu.addItem (3, juce::String ("Scissors")
                         + (activeTool == EditTool::Scissors    ? "  \xe2\x9c\x93" : "")
                         + "   S");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this] (int result) {
                switch (result)
                {
                    case 1: setActiveTool (EditTool::Pointer);     break;
                    case 2: setActiveTool (EditTool::RangeSelect); break;
                    case 3: setActiveTool (EditTool::Scissors);    break;
                    default: break;
                }
            });
        return;  // Don't start marquee on right-click
    }

    if (! e.mods.isLeftButtonDown()) return;

    const int timelineLeft = kHeaderWidth;
    const int timelineTop  = kHeaderStripHeight + kRulerHeight;

    if (e.x < timelineLeft || e.y < timelineTop) return;

    const int xInTimeline = (e.x - timelineLeft) + timelineViewport.getViewPositionX();
    const double clickTimeSecs = juce::jmax (0.0, (double)xInTimeline / pixelsPerSecond);

    switch (activeTool)
    {
        case EditTool::Pointer:
            // Seek playhead — click on empty timeline space deselects all clips.
            // (Clip clicks are handled by ClipComponent and never reach here.)
            for (auto* row : trackRows)
                for (int i = 0; i < row->getClipArea().getNumChildComponents(); ++i)
                    if (auto* cc = dynamic_cast<ClipComponent*> (row->getClipArea().getChildComponent (i)))
                        cc->setSelected (false);
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
            // Split clip at the click position. ClipComponent::mouseDown also forwards
            // scissors clicks to this method via findParentComponentOfClass, but direct
            // clicks on empty timeline space are handled here.
            splitClipAtTime (clickTimeSecs, e.y);
            break;
    }

    // Reclaim keyboard focus so E/R/S shortcuts work after any left-click in the arrangement.
    grabKeyboardFocus();

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

            // Wire single-click selection — notifies ArrangementView so Cmd+C/V works
            cc->onSelectRequest = [this] (ClipComponent* clicked)
            {
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

            // Wire inter-track drag: moving the clip >30px vertically triggers a track move
            cc->onInterTrackDrag = [this] (te::Clip& c, double startSecs, int deltaY)
            {
                moveClipByTrackDelta (c, startSecs, deltaY);
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

    if (isTrackChange || isClipChange)
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

    if (isTrackChange || isClipChange)
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

void ArrangementView::moveClipByTrackDelta (te::Clip& clip, double startTimeSecs,
                                             int deltaYPx)
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

    // Calculate target track from vertical pixel delta
    const int rowH = (int)(TrackRow::kCollapsedHeight * trackHeightScale);
    const int rowDelta = deltaYPx / juce::jmax (1, rowH);
    const int targetTrackIndex = juce::jlimit (0, trackRows.size() - 1,
                                               sourceTrackIndex + rowDelta);
    if (targetTrackIndex == sourceTrackIndex)
        return;

    auto* destTrack = dynamic_cast<te::AudioTrack*> (&trackRows[targetTrackIndex]->getTrack());
    if (destTrack == nullptr)
        return;

    // Move the clip to the target track and reposition
    clipEditor.moveClipToTrack (clip, *destTrack);
    clipEditor.moveClip (clip, startTimeSecs);

    // Rebuild after the move (callAsync to avoid re-entrancy from within drag callback)
    juce::MessageManager::callAsync ([this] { rebuildTrackRows(); });
}
