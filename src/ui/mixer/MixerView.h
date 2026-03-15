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
 * Plan 01-17 (EXP-03 meter gap closure):
 *   MixerView now owns a 24fps juce::Timer. Its timerCallback() reads from
 *   AudioEngine::getMasterMeterProcessor() (which taps the post-render output
 *   buffer via an AudioIODeviceCallback) and calls flushFifo() to pump
 *   libebur128 on the message thread, then pushes the resulting values to
 *   all ChannelStrip MeterBars via setMeterValues(). All strips display the
 *   program-level mix in Phase 1 (per-track metering deferred to Phase 2).
 *
 * Plan 01-10 per-track MeterProcessors are retained for the getMeterProcessorForTrack()
 * API — they remain as placeholders for per-track metering in a future phase.
 */
class MixerView : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::Timer
{
public:
    MixerView (ApplicationState& appState, MixerEngine& mixerEngine);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /**
     * Returns the MeterProcessor for a given track.
     * Retained from plan 01-10 for future per-track metering.
     * Returns nullptr if the track has no strip.
     */
    MeterProcessor* getMeterProcessorForTrack (const te::AudioTrack& track);

private:
    ApplicationState& appState;
    MixerEngine& mixerEngine;

    juce::Viewport channelStripViewport;
    juce::Component channelStripContainer;
    juce::OwnedArray<ChannelStrip> strips;

    // Per-track MeterProcessors retained from plan 01-10 (future per-track metering).
    // In Phase 1 the master meter from AudioEngine feeds all strips instead.
    std::map<te::AudioTrack*, std::unique_ptr<MeterProcessor>> meterProcessors;

    static constexpr int kStripWidth = 80;

    void rebuildStrips();

    // juce::Timer — fires at 24fps to flush master meter FIFO and update MeterBars
    void timerCallback() override;

    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged   (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
