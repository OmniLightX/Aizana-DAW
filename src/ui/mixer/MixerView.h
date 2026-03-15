#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"
#include "ChannelStrip.h"

#include <map>
#include <memory>

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
 *
 * Plan 01-10: MixerView owns one MeterProcessor per track. Processors are
 * initialised with prepareToPlay(44100, 512) as default; the audio engine
 * calls prepareToPlay again with real values when it starts.
 */
class MixerView : public juce::Component,
                  private juce::ValueTree::Listener
{
public:
    MixerView (ApplicationState& appState, MixerEngine& mixerEngine);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /**
     * Returns the MeterProcessor for a given track.
     * Used by the audio graph to push samples into the meter FIFO.
     * Returns nullptr if the track has no strip (shouldn't happen in normal use).
     */
    MeterProcessor* getMeterProcessorForTrack (const te::AudioTrack& track);

private:
    ApplicationState& appState;
    MixerEngine& mixerEngine;

    juce::Viewport channelStripViewport;
    juce::Component channelStripContainer;
    juce::OwnedArray<ChannelStrip> strips;

    // One MeterProcessor per track (owned here, referenced by ChannelStrip's MeterBar)
    // Key: track pointer (stable for the lifetime of the strip)
    std::map<te::AudioTrack*, std::unique_ptr<MeterProcessor>> meterProcessors;

    static constexpr int kStripWidth = 80;

    void rebuildStrips();

    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged   (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
