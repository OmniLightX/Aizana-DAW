#include "MixerView.h"

//==============================================================================

MixerView::MixerView (ApplicationState& appState_, MixerEngine& mixerEngine_)
    : appState (appState_)
    , mixerEngine (mixerEngine_)
{
    channelStripViewport.setScrollBarsShown (false, true);  // horizontal scroll only
    channelStripViewport.setViewedComponent (&channelStripContainer, false);
    addAndMakeVisible (channelStripViewport);

    // Register ValueTree listener so we rebuild on track add/remove
    if (auto* edit = appState.editManager.getEdit())
        edit->state.addListener (this);

    rebuildStrips();
}

MixerView::~MixerView()
{
    if (auto* edit = appState.editManager.getEdit())
        edit->state.removeListener (this);
}

void MixerView::resized()
{
    channelStripViewport.setBounds (getLocalBounds());

    // Size container to hold all strips side-by-side
    const int totalW = juce::jmax (strips.size() * kStripWidth,
                                   channelStripViewport.getWidth());
    channelStripContainer.setSize (totalW, channelStripViewport.getMaximumVisibleHeight());

    // Lay out strips horizontally
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * kStripWidth, 0, kStripWidth,
                              channelStripContainer.getHeight());
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Top border separating mixer from arrangement
    g.setColour (juce::Colour (0xff000000));
    g.drawLine (0.0f, 0.0f, (float)getWidth(), 0.0f, 2.0f);
}

void MixerView::rebuildStrips()
{
    channelStripContainer.removeAllChildren();
    strips.clear();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    for (auto* audioTrack : edit->getAudioTracks())
    {
        auto* strip = strips.add (new ChannelStrip (*audioTrack, mixerEngine));
        channelStripContainer.addAndMakeVisible (strip);
    }

    resized();
}

//==============================================================================
// ValueTree::Listener

void MixerView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent, child);
}

void MixerView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent, child);
}

void MixerView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (auto* edit = appState.editManager.getEdit())
    {
        if (parent == edit->state || parent.isAChildOf (edit->state))
            juce::MessageManager::callAsync ([this] { rebuildStrips(); });
    }
    juce::ignoreUnused (parent);
}
