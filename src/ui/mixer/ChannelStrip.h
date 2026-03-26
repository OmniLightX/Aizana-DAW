#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"

// Forward declarations — full includes in ChannelStrip.cpp
class PluginManager;
class PluginWindowManager;
class PluginScanner;

namespace te = tracktion::engine;

//==============================================================================
/**
 * MeterBar — Real-time LUFS/Peak meter display for a ChannelStrip.
 *
 * Reads from MeterProcessor atomics on the message thread at ~24fps.
 * Displays:
 *   - Vertical bar: instantaneous peak level (dBFS)
 *   - Numeric label: integrated LUFS (reference information only —
 *     REQUIREMENTS.md: "LUFS und technische Werte sind Richtlinien, nicht Pflicht-Ziel")
 *
 * No clipping indicators or target enforcement — LUFS is informational only.
 *
 * Thread safety: only reads from std::atomic values (thread-safe).
 */
class MeterBar : public juce::Component
{
public:
    explicit MeterBar (MeterProcessor& meterProcessor);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /**
     * Push new meter values to display (called from MixerView timerCallback on message thread).
     * Stores values and triggers repaint. All MeterBars receive the same master meter values
     * in Phase 1 (program-level metering shared across all strips).
     */
    void setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI);

private:
    MeterProcessor& meterProcessor;

    // Current display values — updated by setMeterValues(), read in paint().
    // All operations on message thread, no synchronisation needed.
    float displayPeakDB = -100.0f;
    float displayRMSDB  = -100.0f;
    float displayLUFSM  = -100.0f;  // momentary LUFS
    float displayLUFSI  = -100.0f;  // integrated LUFS

    // Peak hold: highest peak seen, decays after kPeakHoldMs
    float peakHoldDB    = -100.0f;
    int   peakHoldTicks = 0;
    static constexpr int kPeakHoldTicks = 60;  // ~2.5s at 24fps

    juce::Label lufsLabel;  // Shows integrated LUFS value below the bar

    // Convert dBFS to normalised bar height (0.0 = silent, 1.0 = 0dBFS)
    static float dbToNorm (float dB);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBar)
};

//==============================================================================
/**
 * ChannelStrip — Single mixer channel strip for one AudioTrack.
 *
 * Width: ~80px. Height: full MixerView height.
 *
 * Contains (top to bottom):
 *   - MeterBar  — real-time peak + LUFS meter (plan 01-10, EXP-03)
 *   - Pan knob (Rotary slider, -1.0 to +1.0)
 *   - Volume fader (Vertical slider, -60 to +6 dB)
 *   - Solo (S) and Mute (M) buttons
 *   - InsertSlotPanel — 6 insert slots for FX (plan 01-68)
 *   - Track name label (bottom)
 *
 * All controls are wired to MixerEngine so mutations go through the shared
 * UndoManager (MIX-01 requirement).
 *
 * ValueTree listener: keeps fader/knob positions in sync with engine state
 * when changes come from outside (e.g., undo, AI automation, parameter change).
 */
class ChannelStrip : public juce::Component,
                     private juce::ValueTree::Listener
{
public:
    ChannelStrip (te::AudioTrack& track, MixerEngine& mixerEngine,
                  MeterProcessor& meterProcessor,
                  PluginManager& pluginManager,
                  PluginWindowManager& pluginWindowManager,
                  PluginScanner& pluginScanner);
    ~ChannelStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    te::AudioTrack& getTrack() const noexcept { return track; }

    /** Returns the MeterBar so MixerView::timerCallback can push master meter values. */
    MeterBar& getMeterBar() noexcept { return meterBar; }

    /**
     * Push master meter values to this strip's MeterBar (called from MixerView::timerCallback).
     * All strips receive the same program-level values in Phase 1.
     */
    void setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI);

    // ---- InsertSlotPanel ----

    /**
     * InsertSlotPanel — Studio One-style insert FX slots shown at the bottom of the strip.
     *
     * Displays up to kMaxSlots insert slots vertically.
     * - Empty slot: shows "+" — click to open a popup menu (Built-in FX + Browse)
     * - Occupied slot: shows plugin name — double-click to open editor, right-click to remove
     *
     * Auto-refreshes via ChannelStrip::valueTreeChildAdded/Removed when plugins are changed.
     */
    class InsertSlotPanel : public juce::Component
    {
    public:
        InsertSlotPanel (te::Track& track,
                         PluginManager& pluginManager,
                         PluginWindowManager& pluginWindowManager,
                         PluginScanner& pluginScanner);

        void resized() override;
        void paint (juce::Graphics& g) override;

        /** Rebuild slot display from track's current plugin chain. */
        void refresh();

        static constexpr int kMaxSlots  = 6;
        static constexpr int kSlotHeight = 16;

    private:
        te::Track&           track;
        PluginManager&       pluginManager;
        PluginWindowManager& pluginWindowManager;
        PluginScanner&       pluginScanner;

        struct SlotButton : public juce::Component
        {
            te::Plugin* plugin = nullptr;  // nullptr = empty slot
            std::function<void()> onClick;
            std::function<void()> onDoubleClick;
            std::function<void()> onRightClick;

            void paint (juce::Graphics& g) override;
            void mouseDown (const juce::MouseEvent& e) override;
            void mouseDoubleClick (const juce::MouseEvent& e) override;
        };

        juce::OwnedArray<SlotButton> slots;

        void showAddPluginMenu (int slotIndex);
        void showRemovePluginMenu (te::Plugin* plugin);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertSlotPanel)
    };

private:
    te::AudioTrack& track;
    MixerEngine& mixerEngine;
    PluginManager& pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner& pluginScanner;

    MeterBar         meterBar;
    juce::Slider     volumeFader;
    juce::Slider     panKnob;
    juce::Label      panValueLabel; // numeric pan readout: "L33", "C", "R48"
    juce::Label      panLabel;      // "PAN" label below the pan knob
    juce::TextButton soloButton { "S" };
    juce::TextButton muteButton { "M" };
    juce::Label      trackNameLabel;
    InsertSlotPanel  insertSlotPanel;

    // Called when the fader value changes (user interaction)
    void volumeChanged();
    void panChanged();

    // Format pan value as "L33", "C", or "R48" (absolute percent 0-100)
    static juce::String formatPanValue (double pan);

    // ValueTree::Listener — keeps UI in sync when track state changes externally
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void valueTreeChildAdded       (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved     (juce::ValueTree& parent, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged    (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
