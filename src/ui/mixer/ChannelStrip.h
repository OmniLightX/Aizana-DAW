#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
#include "metering/MeterProcessor.h"

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
class MeterBar : public juce::Component,
                 private juce::Timer
{
public:
    explicit MeterBar (MeterProcessor& meterProcessor);
    ~MeterBar() override;

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

    // Current display values — updated in timerCallback or setMeterValues(), read in paint().
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

    void timerCallback() override;  // Fires at ~24fps

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
 *   - Track name label (bottom)
 *   - Solo (S) and Mute (M) buttons
 *   - Pan knob (Rotary slider, -1.0 to +1.0)
 *   - Volume fader (Vertical slider, -60 to +6 dB)
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
                  MeterProcessor& meterProcessor);
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

private:
    te::AudioTrack& track;
    MixerEngine& mixerEngine;

    MeterBar         meterBar;
    juce::Slider     volumeFader;
    juce::Slider     panKnob;
    juce::Label      panLabel;      // "PAN" label below the pan knob
    juce::TextButton soloButton { "S" };
    juce::TextButton muteButton { "M" };
    juce::Label      trackNameLabel;

    // Called when the fader value changes (user interaction)
    void volumeChanged();
    void panChanged();

    // ValueTree::Listener — keeps UI in sync when track state changes externally
    void valueTreePropertyChanged (juce::ValueTree& tree,
                                   const juce::Identifier& property) override;
    void valueTreeChildAdded       (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved     (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged    (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStrip)
};
