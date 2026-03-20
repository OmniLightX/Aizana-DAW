#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"
#include "ChannelStrip.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"

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
    MixerView (ApplicationState& appState, MixerEngine& mixerEngine,
               PluginManager& pluginManager, PluginWindowManager& pluginWindowManager,
               PluginScanner& pluginScanner);
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
    MixerEngine&      mixerEngine;
    PluginManager&    pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&    pluginScanner;

    juce::Viewport channelStripViewport;
    juce::Component channelStripContainer;
    juce::OwnedArray<ChannelStrip> strips;

    // Per-track MeterProcessors retained from plan 01-10 (future per-track metering).
    // In Phase 1 the master meter from AudioEngine feeds all strips instead.
    std::map<te::AudioTrack*, std::unique_ptr<MeterProcessor>> meterProcessors;

    static constexpr int kStripWidth = 80;
    int cachedStripHeight = 200;  // Never go below 200px; updated on valid resized() calls

    /**
     * BusStrip — Visual representation of a FolderTrack (bus) in the mixer.
     * Simpler than ChannelStrip: shows bus name and a coloured header bar.
     * Volume on bus is controlled via the track's VolumePlugin if available,
     * otherwise display-only. Created when a FolderTrack exists in the edit.
     *
     * Apple approach: buses appear immediately with distinct visual style
     * (darker, tagged "BUS") so users get instant confirmation of creation.
     */
    class BusStrip : public juce::Component
    {
    public:
        BusStrip (te::FolderTrack&     track_,
                  PluginManager&        pluginManager_,
                  PluginWindowManager&  pluginWindowManager_,
                  PluginScanner&        pluginScanner_)
            : track (track_)
            , pluginManager (pluginManager_)
            , pluginWindowManager (pluginWindowManager_)
            , pluginScanner (pluginScanner_)
        {
            nameLabel.setText ("BUS\n" + track.getName(), juce::dontSendNotification);
            nameLabel.setJustificationType (juce::Justification::centred);
            nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xffbbbbbb));
            nameLabel.setFont (juce::FontOptions (10.0f));
            addAndMakeVisible (nameLabel);

            // Ensure this component receives mouse events even when child
            // components (e.g. nameLabel) are present. The 'false' argument
            // means children do not absorb clicks intended for the parent.
            setInterceptsMouseClicks (true, false);
        }

        void resized() override
        {
            nameLabel.setBounds (getLocalBounds().reduced (4));
        }

        void paint (juce::Graphics& g) override
        {
            // Darker background distinguishes buses from audio track strips
            g.fillAll (juce::Colour (0xff1a1a2e));

            // Bus colour strip at top (4px) — uses track colour like ChannelStrip
            g.setColour (track.getColour());
            g.fillRect (0, 0, getWidth(), 4);

            // "BUS" tag overlay in top-left corner
            g.setColour (juce::Colour (0xff446688));
            g.fillRoundedRectangle (4.0f, 6.0f, 28.0f, 12.0f, 3.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (8.0f));
            g.drawText ("BUS", 4, 6, 28, 12, juce::Justification::centred);

            // Right border separator
            g.setColour (juce::Colour (0xff111111));
            g.drawLine ((float)getWidth() - 1.0f, 0.0f,
                        (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override;

    private:
        te::FolderTrack&      track;
        PluginManager&        pluginManager;
        PluginWindowManager&  pluginWindowManager;
        PluginScanner&        pluginScanner;
        juce::Label           nameLabel;

        void showContextMenu();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusStrip)
    };

    juce::OwnedArray<BusStrip> busStrips;

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
