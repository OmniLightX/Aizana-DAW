#include "ArrangementView.h"

//==============================================================================

ArrangementView::ArrangementView (ApplicationState& appState_,
                                  ClipEditor& clipEditor_,
                                  MixerEngine& mixerEngine_)
    : appState (appState_)
    , clipEditor (clipEditor_)
    , mixerEngine (mixerEngine_)
{
    // Header viewport — scrolls vertically only (no horizontal scroll)
    headerViewport.setScrollBarsShown (true, false);
    headerViewport.setViewedComponent (&headerContainer, false);
    addAndMakeVisible (headerViewport);

    // Timeline viewport — scrolls both horizontally and vertically
    timelineViewport.setScrollBarsShown (true, true);
    timelineViewport.setViewedComponent (&timelineContainer, false);
    addAndMakeVisible (timelineViewport);

    // Keep vertical scroll positions in sync between header and timeline
    timelineViewport.getVerticalScrollBar().onValueChange = [this]
    {
        headerViewport.setViewPosition (0,
            (int)timelineViewport.getVerticalScrollBar().getCurrentRangeStart());
    };
    headerViewport.getVerticalScrollBar().onValueChange = [this]
    {
        timelineViewport.setViewPosition (
            timelineViewport.getViewPositionX(),
            (int)headerViewport.getVerticalScrollBar().getCurrentRangeStart());
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
    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);
}

void ArrangementView::resized()
{
    auto area = getLocalBounds();

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
            for (auto* t : edit->getAudioTracks())
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

    for (auto* audioTrack : edit->getAudioTracks())
    {
        // Create and add track header
        auto* header = trackHeaders.add (new TrackHeader (*audioTrack, mixerEngine));
        headerContainer.addAndMakeVisible (header);

        // Create clip components for each clip in this track
        for (auto* clip : audioTrack->getClips())
        {
            auto* cc = clipComponents.add (new ClipComponent (*clip));
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
