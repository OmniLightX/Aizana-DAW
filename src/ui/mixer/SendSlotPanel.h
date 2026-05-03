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
    static constexpr int kRowHeight   = 16;
    static constexpr int kVisibleRows = 3;

    SendSlotPanel (te::AudioTrack& track, MixerEngine& mixerEngine);

    /** Rebuild send rows from the given FX channel list. Call whenever channels change. */
    void rebuild (const juce::Array<te::AudioTrack*>& fxChannels);

    /** Returns the number of send rows currently displayed. */
    int getNumRows() const { return rows.size(); }

    void resized() override;
    void paint   (juce::Graphics& g) override;

private:
    te::AudioTrack& track;
    MixerEngine&    mixerEngine;

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
