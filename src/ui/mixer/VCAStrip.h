#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"

namespace te = tracktion::engine;

/**
 * VCAStrip — mixer strip for a VCA channel (group fader, gold-amber accent).
 *
 * Aizana VCA semantics: the strip's fader moves all member tracks' faders
 * RELATIVELY (dB delta from the drag start, preserving balance) — the same
 * mechanic as the multi-select group fader, but with a persistent membership
 * stored on the member tracks (kVCAAssignmentProp). No audio flows through
 * the VCA track itself.
 *
 * Layout:  name | members button | (space) | S/M | fader
 *   - "Zuweisen ▾" opens a tick-menu of all plain audio tracks
 *   - S/M propagate to all members
 */
class VCAStrip : public juce::Component
{
public:
    VCAStrip (te::AudioTrack& vcaTrack, MixerEngine& mixerEngine);
    ~VCAStrip() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    te::AudioTrack& getTrack() const { return track; }
    void setSelected (bool s);
    void refresh();

    std::function<void (te::AudioTrack*)> onTrackSelected;
    /** Fired after a member's fader moved through the VCA — MixerView refreshes strips. */
    std::function<void()> onMembersChanged;

private:
    te::AudioTrack& track;
    MixerEngine&    mixerEngine;
    bool isSelected = false;

    juce::Label      nameLabel;
    juce::TextButton membersButton { juce::String::fromUTF8 ("Zuweisen ▾") };
    juce::TextButton soloButton { "S" };
    juce::TextButton muteButton { "M" };
    juce::Slider     fader;

    // Relative drive: snapshots at drag start
    double dragStartDb = 0.0;
    std::vector<std::pair<te::AudioTrack*, double>> memberStartDb;

    void beginVcaDrag();
    void applyVcaDrag (double newDb);
    void showMembersMenu();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VCAStrip)
};
