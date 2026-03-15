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

    /** Shows the edit BPM — e.g. "120 BPM". Double-click to edit. */
    juce::Label bpmLabel;

    /** Shows the audio device sample rate — e.g. "44.1 kHz". */
    juce::Label sampleRateLabel;

    /** Counter for throttling sample-rate polls (update every ~2s at 30ms timer). */
    int sampleRateUpdateCounter = 0;

    void timerCallback() override;

    /** Refresh bpmLabel text from the current edit tempo. */
    void updateBpmLabel();

    void playClicked();
    void stopClicked();
    void recordClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
