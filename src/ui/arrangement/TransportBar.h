#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/ApplicationState.h"
#include "ui/common/IconButton.h"

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
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void showSampleRateMenu();
    ApplicationState& appState;

    // Transport controls — vector-icon buttons (Sketch 005 Pro-UI polish; no text labels).
    IconButton rewindButton    { CloudesIcon::Rewind };     // Return to start (|◀)
    IconButton playButton      { CloudesIcon::Play };
    IconButton stopButton      { CloudesIcon::Stop };
    IconButton recordButton    { CloudesIcon::Record };

    /** Enables/disables the metronome click track. */
    IconButton metronomeButton { CloudesIcon::Metronome };

    /** Enables/disables loop playback; lit when transport.looping is true. */
    IconButton loopButton { CloudesIcon::Loop };

    /** Tap Tempo — average of last 4 taps; >2s gap resets sequence. */
    IconButton tapTempoButton { CloudesIcon::Tap };

    /** The Dial (Phase 2, Block A) — switch to the Einsteiger-Welt + assist toggle.
     *  Only visible in the Pro view, so the mode button always means "go to Beginner". */
    juce::TextButton modeButton   { "Einsteiger" };
    juce::TextButton assistButton { "Assist" };

    // Tap tempo state — last 4 tap timestamps (juce::Time::getMillisecondCounterHiRes)
    juce::Array<double> tapTimes;

    void tapTempoClicked();

    /** Shows current playback position — e.g. "0:00:00.000". Updated every 30ms. */
    juce::Label positionLabel;

    /** Shows the edit BPM — e.g. "120 BPM". Double-click to edit. */
    juce::Label bpmLabel;

    /** Time signature numerator selector (2, 3, 4, 5, 6, 7, 8, 9, 12). */
    juce::ComboBox timeSigNumerator;

    /** "/" separator between numerator and denominator. */
    juce::Label timeSigSlash;

    /** Time signature denominator selector (2, 4, 8, 16). */
    juce::ComboBox timeSigDenominator;

    /** Shows the audio device sample rate — e.g. "44.1 kHz". */
    juce::Label sampleRateLabel;

    /** Counter for throttling sample-rate polls (update every ~2s at 30ms timer). */
    int sampleRateUpdateCounter = 0;

    void timerCallback() override;

    /** Refresh bpmLabel text from the current edit tempo. */
    void updateBpmLabel();

    /** Refresh time signature combos from the current edit time signature. */
    void updateTimeSigDisplay();

    void rewindClicked();
    void playClicked();
    void stopClicked();
    void recordClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
