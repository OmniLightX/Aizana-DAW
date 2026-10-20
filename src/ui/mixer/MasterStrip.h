#pragma once
#include "ui/common/MixerControls.h"
#include "InsertRack.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "metering/MeterProcessor.h"

// Forward declarations
class PluginWindowManager;
class PluginScanner;

namespace te = tracktion::engine;

/**
 * MasterStrip — Permanent master bus channel strip in the Mixer.
 *
 * Represents the Edit's built-in master output: all AudioTrack signals sum into
 * this node in Tracktion Engine's render graph. Wired to:
 *   - edit.getMasterVolumePlugin()  — fader and ValueTree listener for undo sync
 *   - edit.getMasterPluginList()    — insert slots for mastering plugins
 *   - masterMeter (MeterProcessor)  — real-time peak + LUFS meter display
 *
 * Visual identity:
 *   - Gold colour strip at top (4px), "MASTER" label at bottom
 *   - Slightly wider than a ChannelStrip (same kStripWidth, distinct colour)
 *   - No Solo/Mute (the master cannot be soloed or muted by the user)
 *
 * Lifecycle: created by MixerView::rebuildStrips() whenever a valid Edit exists.
 * Displayed rightmost in the mixer, after all AudioTrack strips.
 */
class MasterStrip : public juce::Component,
                    private juce::ValueTree::Listener
{
public:
    MasterStrip (te::Edit& edit,
                 MeterProcessor& masterMeter,
                 PluginWindowManager& pluginWindowManager,
                 PluginScanner& pluginScanner);
    ~MasterStrip() override;

    void resized() override;
    void paint  (juce::Graphics& g) override;

    /** Push master meter values (called from MixerView::timerCallback). */
    void setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI);

    // -------------------------------------------------------------------------
    // MasterInsertSlotPanel — insert slots backed by Edit::getMasterPluginList()
    // -------------------------------------------------------------------------
    class MasterInsertSlotPanel : public InsertRack
    {
    public:
        MasterInsertSlotPanel(te::Edit& e, PluginWindowManager& w, PluginScanner& s)
            : InsertRack(e.getMasterPluginList(),w,s) {}
    };

private:
    te::Edit&            edit;
    MeterProcessor&      masterMeter;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;

    // ---- Reuse MeterBar's display logic via composition ----
    // We duplicate the MeterBar fields here rather than including ChannelStrip.h
    // to avoid a circular dependency chain (ChannelStrip.h includes MeterProcessor.h
    // which is fine, but ChannelStrip also includes MixerEngine.h etc.).
    // A lightweight meter component is simpler here.
    struct MeterDisplay : public juce::Component
    {
        float peakDB  = -100.0f;
        float holdDB  = -100.0f;
        int   holdAge = 0;
        static constexpr int kHoldTicks = 60;

        juce::Label peakLabel;
        juce::Label lufsLabel;

        MeterDisplay();
        void setValues (float peak, float lufsI);
        void paint (juce::Graphics& g) override;
        void resized() override;

        static float dbToNorm (float db);
    };

    aizana::MixerFader      volumeFader;
    MeterDisplay      meterDisplay;
    MasterInsertSlotPanel insertPanel;
    juce::Label       nameLabel;

    void volumeChanged();

    // ValueTree::Listener — sync fader on undo/redo and plugin list changes
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void valueTreeChildAdded   (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged     (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterStrip)
};
