#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"

namespace te = tracktion::engine;

/**
 * SendSlotPanel — compact per-track FX send rows.
 *
 * Shown below insert slots in ChannelStrip. One row per FX Channel.
 * Each row: [●/○ dot toggle][channel name][horizontal level slider]
 *
 * Collapses to zero height when there are no send rows (ChannelStrip
 * allocates 0px via kSendH when getNumRows() == 0).
 *
 * No PRE/POST toggle in the default view — add via right-click if needed later.
 */
class SendSlotPanel : public juce::Component
{
public:
    static constexpr int kRowHeight   = 20;
    static constexpr int kVisibleRows = 3;

    SendSlotPanel (te::AudioTrack& track, MixerEngine& mixerEngine);

    /** Rebuild send rows from the given FX channel list. Call whenever channels change. */
    void rebuild (const juce::Array<te::AudioTrack*>& fxChannels);

    /** Returns the number of send rows currently displayed. */
    int getNumRows() const { return rows.size(); }

    /** Height the strip should allocate: send rows + the "+ Send" row,
        capped at kVisibleRows (scrolls beyond). Always ≥ 1 row so sends are
        discoverable even before any FX channel exists (Studio One parity —
        live-test finding 2026-07-03: users could not create more sends). */
    int getPreferredHeight() const
    {
        return juce::jlimit (1, kVisibleRows, rows.size() + 1) * kRowHeight;
    }

    void resized() override;
    void paint   (juce::Graphics& g) override;

private:
    te::AudioTrack& track;
    MixerEngine&    mixerEngine;

    juce::Array<te::AudioTrack*> sendTargets;   // FX channels offered in the "+ Send" menu
    juce::TextButton addSendButton { "+ Send" };

    void showAddSendMenu();
    void promptNewFxChannel();

    struct SendRow : public juce::Component
    {
        SendRow (te::AudioTrack& sourceTrack,
                 te::AudioTrack& fxChannel,
                 MixerEngine&    mixerEngine);

        void resized() override;
        void paint   (juce::Graphics& g) override;

        te::AudioTrack& fxChannel;

    private:
        te::AudioTrack& sourceTrack;
        MixerEngine&    mixerEngine;

        juce::Label  nameLabel;
        juce::Slider levelSlider;

        /** Dot toggle button — compact, no text. */
        juce::TextButton dotButton { "" };

        void syncDotColour();
    };

    juce::Viewport              viewport;
    juce::Component             content;
    juce::OwnedArray<SendRow>   rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendSlotPanel)
};
