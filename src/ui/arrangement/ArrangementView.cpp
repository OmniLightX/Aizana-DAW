#include "ArrangementView.h"

//==============================================================================

ArrangementView::ArrangementView (ApplicationState&    appState_,
                                  ClipEditor&          clipEditor_,
                                  MixerEngine&         mixerEngine_,
                                  PluginManager&       pluginManager_,
                                  PluginWindowManager& pluginWindowManager_,
                                  PluginScanner&       pluginScanner_)
    : appState            (appState_)
    , clipEditor          (clipEditor_)
    , mixerEngine         (mixerEngine_)
    , pluginManager       (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner       (pluginScanner_)
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

    // Register as ValueTree listener on the Edit's state so we rebuild when
    // tracks are added/removed — RESEARCH.md Pattern 1 (reactive ValueTree).
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    // Build initial track rows
    rebuildTrackRows();
}

ArrangementView::~ArrangementView()
{
    timelineViewport.getVerticalScrollBar().removeListener (this);
    headerViewport.getVerticalScrollBar().removeListener (this);

    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();

    // Header strip at top: Add Track button (height 28px)
    auto headerStrip = area.removeFromTop (kHeaderStripHeight);
    addTrackButton.setBounds (headerStrip.removeFromLeft (80).reduced (2));

    // Left: track headers panel
    headerViewport.setBounds (area.removeFromLeft (kHeaderWidth));

    // Right: timeline panel (rest of width)
    timelineViewport.setBounds (area);

    // Size the containers based on track count
    const int trackCount = trackHeaders.size();
    const int totalTrackH = trackCount * kTrackHeight;

    headerContainer.setSize (kHeaderWidth, juce::jmax (totalTrackH, headerViewport.getHeight()));

    // Timeline width: 5 minutes at current zoom, or at least the viewport width
    const double timelineWidthPx = pixelsPerSecond * 300.0;   // 5 minutes
    timelineContainer.setSize (juce::jmax ((int)timelineWidthPx, timelineViewport.getWidth()),
                               juce::jmax (totalTrackH, timelineViewport.getHeight()));

    // Lay out each track header vertically in the header container
    for (int i = 0; i < trackHeaders.size(); ++i)
        trackHeaders[i]->setBounds (0, i * kTrackHeight, kHeaderWidth, kTrackHeight);

    // Lay out clip components inside timelineContainer
    for (auto* cc : clipComponents)
    {
        auto& clip = cc->getClip();
        auto* parentTrack = clip.getTrack();
        if (parentTrack == nullptr)
            continue;

        // Find track row index
        int rowIndex = -1;
        auto* edit = appState.editManager.getEdit();
        if (edit != nullptr)
        {
            int idx = 0;
            for (auto* t : te::getAudioTracks (*edit))
            {
                if (t == parentTrack)
                {
                    rowIndex = idx;
                    break;
                }
                ++idx;
            }
        }

        if (rowIndex < 0)
            continue;

        const double startSecs = clip.getPosition().getStart().inSeconds();
        const double endSecs   = clip.getPosition().getEnd().inSeconds();
        const int x  = (int)(startSecs * pixelsPerSecond);
        const int w  = juce::jmax (4, (int)((endSecs - startSecs) * pixelsPerSecond));
        const int y  = rowIndex * kTrackHeight + 2;
        const int h  = kTrackHeight - 4;

        cc->setBounds (x, y, w, h);
    }
}

void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Draw a blue highlight border when audio files are being dragged over
    if (isDragHovering)
    {
        g.setColour (juce::Colours::dodgerblue.withAlpha (0.4f));
        g.fillRect (getLocalBounds());
        g.setColour (juce::Colours::dodgerblue);
        g.drawRect (getLocalBounds(), 2);
    }
}

void ArrangementView::setPixelsPerSecond (double pps)
{
    pixelsPerSecond = juce::jlimit (10.0, 2000.0, pps);
    resized();
    repaint();
}

void ArrangementView::rebuildTrackRows()
{
    // Clear existing children
    headerContainer.removeAllChildren();
    timelineContainer.removeAllChildren();
    trackHeaders.clear();
    clipComponents.clear();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    for (auto* audioTrack : te::getAudioTracks (*edit))
    {
        // Create and add track header — now passes plugin management references
        // so that the right-click context menu can insert plugins (plan 01-15)
        auto* header = trackHeaders.add (new TrackHeader (*audioTrack,
                                                          mixerEngine,
                                                          pluginManager,
                                                          pluginWindowManager,
                                                          pluginScanner));
        headerContainer.addAndMakeVisible (header);

        // Create clip components for each clip in this track
        for (auto* clip : audioTrack->getClips())
        {
            auto* cc = clipComponents.add (new ClipComponent (*clip, appState.editManager));
            timelineContainer.addAndMakeVisible (cc);
        }
    }

    // Trigger layout
    resized();
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
    // Clip/track property changes (name, position) may require a repaint
    juce::MessageManager::callAsync ([this] { resized(); repaint(); });
}

//==============================================================================
// ScrollBar::Listener — keeps header and timeline vertical scroll in sync

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
    // dropY is relative to ArrangementView; subtract the header strip height to get
    // the y within the track-rows area.
    int rowY = dropY - kHeaderStripHeight;
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
