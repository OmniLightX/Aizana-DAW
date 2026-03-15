#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"

namespace te = tracktion::engine;

/**
 * ChannelStrip — Single mixer channel strip for one AudioTrack.
 *
 * Width: ~80px. Height: full MixerView height.
 *
 * Contains (top to bottom):
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
    ChannelStrip (te::AudioTrack& track, MixerEngine& mixerEngine);
    ~ChannelStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    te::AudioTrack& getTrack() const noexcept { return track; }

private:
    te::AudioTrack& track;
    MixerEngine& mixerEngine;

    juce::Slider  volumeFader;
    juce::Slider  panKnob;
    juce::TextButton soloButton { "S" };
    juce::TextButton muteButton { "M" };
    juce::Label   trackNameLabel;

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
