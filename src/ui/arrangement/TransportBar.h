#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/ApplicationState.h"

/**
 * TransportBar — Fixed transport control bar at the top of the DAW window.
 *
 * Displays Play, Stop, Record buttons, a position indicator (updated every ~30ms),
 * and a BPM display. Wired directly to the Edit's TransportControl.
 *
 * Layout (CONTEXT.md): fixed height ~50px, full window width.
 *
 * Thread model: All UI updates happen on the message thread via Timer.
 */
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar (ApplicationState& appState);
    ~TransportBar() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    ApplicationState& appState;

    juce::TextButton playButton   { "Play" };
    juce::TextButton stopButton   { "Stop" };
    juce::TextButton recordButton { "Rec"  };

    /** Shows current playback position — e.g. "0:00:00.000". Updated every 30ms. */
    juce::Label positionLabel;

    /** Shows the edit BPM — e.g. "120 BPM". */
    juce::Label bpmLabel;

    void timerCallback() override;

    void playClicked();
    void stopClicked();
    void recordClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
