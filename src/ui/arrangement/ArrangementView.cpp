#include "ArrangementView.h"

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
                    g.drawVerticalLine (bx, (float)(getHeight() / 2), (float)getHeight());
                }
            }
        }
    }
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

    // Ruler strip — full width, sits between header strip and track rows
    // The ruler portion under the header column (left of kHeaderWidth) is filled
    // by paint() to match the ruler colour, keeping alignment clean.
    auto rulerArea = area.removeFromTop (kRulerHeight);
    rulerStrip.setBounds (rulerArea.withTrimmedLeft (kHeaderWidth));

    // Left: track headers panel
    headerViewport.setBounds (area.removeFromLeft (kHeaderWidth));

    // Right: timeline panel (rest of width)
    timelineViewport.setBounds (area);

    // Size the header container — one row per track header (fixed kTrackHeight each)
    const int trackCount  = trackHeaders.size();
    const int totalHeaderH = trackCount * kTrackHeight;

    headerContainer.setSize (kHeaderWidth,
                             juce::jmax (totalHeaderH, headerViewport.getHeight()));

    // Timeline width: 5 minutes at current zoom, or at least the viewport width
    const double timelineWidthPx = pixelsPerSecond * 300.0;   // 5 minutes

    // Lay out track headers (fixed height, independent of expanded TrackRows)
    for (int i = 0; i < trackHeaders.size(); ++i)
        trackHeaders[i]->setBounds (0, i * kTrackHeight, kHeaderWidth, kTrackHeight);

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
}

void ArrangementView::setPixelsPerSecond (double pps)
{
    pixelsPerSecond = juce::jlimit (10.0, 2000.0, pps);
    resized();
    repaint();
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

    // Auto-follow playhead during playback: if playhead approaches right edge, scroll
    if (edit->getTransport().isPlaying())
    {
        const double visibleEndSecs = scrollPositionSecs
            + timelineViewport.getWidth() / pixelsPerSecond;
        if (playheadPositionSecs > visibleEndSecs - 1.0)
        {
            const int newScrollX = (int)((playheadPositionSecs - 2.0) * pixelsPerSecond);
            timelineViewport.setViewPosition (juce::jmax (0, newScrollX),
                                              timelineViewport.getViewPositionY());
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

    return false;
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

        // TrackRow (right panel / timeline) — expandable, holds clips + AutomationLane
        auto* row = trackRows.add (new TrackRow (*audioTrack,
                                                  automationEngine,
                                                  pixelsPerSecond,
                                                  scrollPositionSecs));
        timelineContainer.addAndMakeVisible (row);

        // Create clip components inside the row's clip area
        for (auto* clip : audioTrack->getClips())
        {
            auto* cc = new ClipComponent (*clip, appState.editManager);
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
    // dropX is relative to ArrangementView; subtract header column width to get
    // timeline-local x, then add current scroll offset.
    int timelineX = dropX - kHeaderWidth;
    double dropTimeSecs = scrollPositionSecs + (timelineX / pixelsPerSecond);
    dropTimeSecs = juce::jmax (0.0, dropTimeSecs);

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
