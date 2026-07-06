#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "SendSlotPanel.h"

namespace te = tracktion::engine;

/**
 * BusFXStrip — mixer channel strip for Bus Channel and FX Channel tracks.
 *
 * Both Bus and FX channels share the same visual layout:
 *   fader + meter | insert slots | name label
 *
 * The accent colour distinguishes them:
 *   Bus Channel → blue (0xff0055cc)
 *   FX Channel  → purple (0xff882299)
 *   Cue Channel → teal (0xff1f9e8e) — P6 headphone mix
 *
 * Bus Channels use output redirection (TrackOutput::setOutputToTrack).
 * FX Channels use AuxSend/Return parallel routing.
 * Cue Channels are FX-style returns whose output feeds a selectable hardware
 * output pair — they additionally show an output-device button.
 * All are otherwise identical in the mixer UI.
 */
class BusFXStrip : public juce::Component,
                   private juce::ValueTree::Listener
{
public:
    BusFXStrip (te::AudioTrack& track,
                MixerEngine& mixerEngine,
                MeterProcessor& meterProcessor,
                PluginManager& pluginManager,
                PluginWindowManager& pluginWindowManager,
                PluginScanner& pluginScanner);
    ~BusFXStrip() override;

    void resized() override;
    void paint  (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    te::AudioTrack& getTrack() const { return track; }

    /** Called by MixerView when this strip is selected or deselected. */
    void setSelected (bool s);

    /** Callback invoked on mouseDown — MixerView wires this to its selection handler. */
    std::function<void (te::AudioTrack*)> onTrackSelected;

    /** Called from MixerView::timerCallback with updated meter values. */
    void setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI);

    /** Sync solo/mute button visual state from current engine state. */
    void refresh();

    /** Callbacks fired after solo/mute button clicks — MixerView wires these for bus-group propagation. */
    std::function<void()> onSoloClicked;
    std::function<void()> onMuteClicked;

    /**
     * Rebuild send rows from the current FX channel list (Studio One pattern:
     * bus→bus sends — e.g. vocal bus → reverb bus, delay bus → reverb bus).
     * This strip's own track is filtered out (no self-send/feedback).
     * Called by MixerView after creating/removing an FX Channel.
     */
    void rebuildSends (const juce::Array<te::AudioTrack*>& fxChannels);

private:
    te::AudioTrack&       track;
    MixerEngine&          mixerEngine;
    PluginManager&        pluginManager;
    PluginWindowManager&  pluginWindowManager;
    PluginScanner&        pluginScanner;

    bool          isSelected = false;

    // ---- Controls ----
    juce::Slider     volumeFader;
    juce::TextButton soloButton { "S" };
    juce::TextButton muteButton { "M" };
    juce::TextButton outputButton;   // cue strips only: "▶ <device>" hardware-output selector
    juce::Label      trackNameLabel;

    // ---- Meter (reuses MeterBar from ChannelStrip via inline paint) ----
    struct MiniMeter : public juce::Component
    {
        float peakDB = -100.0f;
        float holdDB = -100.0f;
        int   holdAge = 0;
        static constexpr int kHoldTicks = 48;

        void setValues (float peak);
        void paint (juce::Graphics& g) override;

        static float dbToNorm (float db);
    };
    MiniMeter meterDisplay;

    // ---- Insert slot panel (shared logic, thin wrapper) ----
    struct InsertSlotPanel : public juce::Component
    {
        static constexpr int kSlotHeight    = 18;
        static constexpr int kVisibleSlots  = 6;

        struct SlotButton : public juce::Component
        {
            te::Plugin* plugin = nullptr;
            std::function<void()> onClick, onDoubleClick, onRightClick;
            std::function<void (const juce::MouseEvent&)> onDragMove, onDragEnd;
            void paint (juce::Graphics& g) override;
            void mouseDown (const juce::MouseEvent& e) override;
            void mouseDrag (const juce::MouseEvent& e) override;
            void mouseUp   (const juce::MouseEvent& e) override;
            void mouseDoubleClick (const juce::MouseEvent&) override;

        private:
            bool dragging = false;
        };

        InsertSlotPanel (te::AudioTrack& track_,
                         MixerEngine& me,
                         PluginManager& pm,
                         PluginWindowManager& pwm,
                         PluginScanner& ps);

        void resized() override;
        void paint (juce::Graphics& g) override;
        void paintOverChildren (juce::Graphics& g) override;  // drag-reorder insertion line
        void refresh();

    private:
        te::AudioTrack&       track;
        MixerEngine&          mixerEngine;
        PluginManager&        pluginManager;
        PluginWindowManager&  pluginWindowManager;
        PluginScanner&        pluginScanner;

        juce::Viewport          slotsViewport;
        juce::Component         slotsContent;
        juce::OwnedArray<SlotButton> slots;

        int dragTargetSlot = -1;   // visible slot the drag currently hovers (-1 = no drag)

        void showAddPluginMenu (int slotIndex);
        void showSlotContextMenu (te::Plugin* plugin);   // Remove + Sidechain source
        int  slotIndexForEvent (const juce::MouseEvent& e);
        void commitSlotDrag (int fromSlot, int toSlot);
    };

    InsertSlotPanel insertSlotPanel;
    SendSlotPanel   sendSlotPanel;

    // ---- ValueTree listener ----
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    void volumeChanged();
    void soloClicked();
    void muteClicked();

    // ---- Cue output selection (P6) ----
    void updateOutputButtonText();
    void showOutputDeviceMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusFXStrip)
};
