#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/MixerEngine.h"
#include "ChannelStrip.h"

namespace te = tracktion::engine;

/**
 * MixerView — Horizontal panel of channel strips.
 *
 * One ChannelStrip per AudioTrack in the active Edit.
 * Horizontally scrollable via juce::Viewport (many tracks may exceed window width).
 *
 * Default visibility: shown on launch (CONTEXT.md: mixing-first design).
 * Toggled by MainComponent on Cmd+M / Ctrl+M.
 *
 * Reactive: listens to Edit's ValueTree for track add/remove to rebuild strips.
 */
class MixerView : public juce::Component,
                  private juce::ValueTree::Listener
{
public:
    MixerView (ApplicationState& appState, MixerEngine& mixerEngine);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    ApplicationState& appState;
    MixerEngine& mixerEngine;

    juce::Viewport channelStripViewport;
    juce::Component channelStripContainer;
    juce::OwnedArray<ChannelStrip> strips;

    static constexpr int kStripWidth = 80;

    void rebuildStrips();

    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged   (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
