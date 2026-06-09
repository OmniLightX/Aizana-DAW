#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"
#include "ChannelStrip.h"
#include "BusFXStrip.h"
#include "MasterStrip.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"

#include <map>
#include <memory>

namespace te = tracktion::engine;

/**
 * MixerView — Horizontal panel of channel strips.
 *
 * Strip order (left → right):
 *   1. Audio track strips (ChannelStrip) — one per plain audio track
 *   2. Bus channel strips (BusFXStrip, blue accent) — submix buses
 *   3. FX channel strips (BusFXStrip, purple accent) — parallel FX returns
 *   4. Master strip (MasterStrip, gold accent) — always rightmost, pinned outside viewport
 *
 * Right-click on the mixer background opens a context menu:
 *   "Add Bus Channel" — creates a new Bus Channel (TrackOutput redirect routing)
 *   "Add FX Channel"  — creates a new FX Channel (AuxSend/Return parallel routing)
 *
 * Bus/FX channels are tagged via kChannelTypeProp and are hidden from ArrangementView.
 *
 * Meter updates: 24fps timer pushes values to all strips via setMeterValues().
 */
class MixerView : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::Timer
{
public:
    MixerView (ApplicationState& appState, MixerEngine& mixerEngine,
               PluginManager& pluginManager, PluginWindowManager& pluginWindowManager,
               PluginScanner& pluginScanner);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /**
     * Returns the MeterProcessor for a given track.
     * Retained from plan 01-10 for future per-track metering.
     * Returns nullptr if the track has no strip.
     */
    MeterProcessor* getMeterProcessorForTrack (const te::AudioTrack& track);

private:
    ApplicationState& appState;
    MixerEngine&      mixerEngine;
    PluginManager&    pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&    pluginScanner;

    juce::Viewport channelStripViewport;

    // Custom container that forwards right-clicks to the MixerView context menu.
    // A plain juce::Component would swallow mouseDown before it bubbles to MixerView.
    struct StripContainer : public juce::Component
    {
        std::function<void(const juce::MouseEvent&)> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick)
                onRightClick (e);
        }
    } channelStripContainer;

    // Audio track strips (plain audio channels)
    juce::OwnedArray<ChannelStrip> strips;

    // Bus and FX channel strips — same visual class, different accent colour
    juce::OwnedArray<BusFXStrip> busFXStrips;

    // Per-track MeterProcessors — one per strip, fed via LevelMeterPlugin peak
    // data synthesised into 512-sample blocks so libebur128 produces per-track LUFS.
    std::map<te::AudioTrack*, std::unique_ptr<MeterProcessor>> meterProcessors;

    // Per-track LevelMeasurer clients — registered with each track's built-in
    // LevelMeterPlugin::measurer so the audio thread fills them with peak dBFS
    // data on every render block. Read and cleared in timerCallback().
    std::map<te::AudioTrack*, std::unique_ptr<te::LevelMeasurer::Client>> levelClients;

    struct TrackInputLevelClient
    {
        te::InputDevice* device = nullptr;
        std::unique_ptr<te::LevelMeasurer::Client> client;
    };

    // Armed tracks should show their hardware input level even when software
    // monitoring is not explicitly enabled. This mirrors professional DAW
    // input meters and avoids coupling metering to the track output graph.
    std::map<te::AudioTrack*, TrackInputLevelClient> inputLevelClients;

    static constexpr int kStripWidth       = 120;  // Audio track and Bus/FX channel strips
    static constexpr int kMasterStripWidth = 140;  // Master strip wider to avoid label truncation
    int cachedStripHeight = 200;  // Never go below 200px; updated on valid resized() calls

    // Master channel strip — permanent, rightmost, always visible when an Edit is active.
    std::unique_ptr<MasterStrip> masterStrip;

    // Currently selected tracks in the Mixer (empty = nothing selected).
    // Supports multi-selection: Cmd+A selects all, Cmd+click adds/removes.
    juce::Array<te::AudioTrack*> selectedMixerTracks;

    // Selected ChannelStrips (regular audio tracks) — subset of selectedMixerTracks.
    juce::Array<ChannelStrip*>   selectedStrips;

    // Selected BusFXStrips — subset of selectedMixerTracks.
    juce::Array<BusFXStrip*>     selectedBusFXStrips;

    void rebuildStrips();

    // Select a track strip; Cmd held = add/remove from multi-selection.
    void setMixerSelectedTrack (te::AudioTrack* t);

    // ---- Multi-selection group fader (Studio One-style relative move) ----
    // When the anchor strip is part of a multi-selection, dragging its fader moves
    // all selected strips by the same dB delta, preserving their relative balance.
    bool   faderGroupActive       = false;
    double faderGroupAnchorStart  = 0.0;                     // anchor's dB at drag start
    std::map<ChannelStrip*, double> faderGroupStart;          // each selected strip's dB at drag start
    void beginFaderGroup (ChannelStrip* anchor);
    void applyFaderGroup (ChannelStrip* anchor, double newDb);
    void endFaderGroup();

    /** Rebuild send slot panels on all audio ChannelStrips with the current FX channel list. */
    void rebuildAllSends();

    /** Show right-click context menu for adding Bus/FX channels. */
    void showContextMenu (const juce::MouseEvent& e);

    /** Call refresh() on all selected ChannelStrips and BusFXStrips so visuals reflect engine state. */
    void refreshAllSelectedStrips();

    /** Call refresh() on ALL strips (all ChannelStrips + BusFXStrips). */
    void refreshAllStrips();

    void detachInputLevelClients();
    float getArmedInputPeakDB (te::AudioTrack& track);

    /**
     * Returns all regular audio tracks that route their main output to the same Bus Channel
     * as @p track.  Includes @p track itself.  Returns an empty array if the track does not
     * route to a bus (i.e. goes directly to master).
     */
    juce::Array<te::AudioTrack*> getTracksInSameBusGroup (te::AudioTrack& track) const;

    // juce::Timer — fires at 24fps to flush master meter FIFO and update MeterBars
    void timerCallback() override;

    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreePropertyChanged   (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
