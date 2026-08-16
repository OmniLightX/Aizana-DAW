#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/ApplicationState.h"
#include "engine/CountIn.h"
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

    /** I-11: Der Edit wird gleich zerstoert. Ein laufender Vorlauf haelt
        geliehenen Transport-Zustand — den vergessen wir hier, statt ihn an
        einem sterbenden Edit zurueckgeben zu wollen. */
    void detachFromEdit();

    /** Neuer Edit ist da: Klick-Schalter uebernehmen, TEs globalen
        Vorzaehler-Modus in den Ruhezustand bringen. */
    void attachToEdit();

    /** Edit/Mix view switcher (Studio One style, adapted to Aizana' stacked
     *  layout): Edit = arrangement only, Mix = mixer shown large. Wired by
     *  MainComponent, which owns the mixer visibility. */
    std::function<void (bool mixFocus)> onSelectViewMode;
    /** Polled by the timer to highlight the active segment (Cmd+M / divider
     *  can change the state without going through the buttons). */
    std::function<bool()> isMixerShown;
    /** Polled alongside isMixerShown — true when the docked editor is open. */
    std::function<bool()> isEditorShown;

    /** KI-Mix co-pilot trigger — wired by MainComponent to startMix()
     *  (Beginner = instant, Pro = preview dialog). */
    std::function<void()> onMixRequested;

    /** KI-Studio — opens the in-app AI assistant chat (wired by MainComponent to openAiStudio()). */
    std::function<void()> onAiStudioRequested;

private:
    void showSampleRateMenu();
    ApplicationState& appState;

    // Transport controls — vector-icon buttons (Sketch 005 Pro-UI polish; no text labels).
    IconButton rewindButton    { AizanaIcon::Rewind, juce::String::fromUTF8 ("Zum Anfang") };     // Return to start (|◀)
    IconButton playButton      { AizanaIcon::Play, juce::String::fromUTF8 ("Wiedergabe") };
    IconButton stopButton      { AizanaIcon::Stop, juce::String::fromUTF8 ("Stopp") };
    IconButton recordButton    { AizanaIcon::Record, juce::String::fromUTF8 ("Aufnahme") };

    /** Enables/disables the metronome click track. */
    IconButton metronomeButton { AizanaIcon::Metronome, juce::String::fromUTF8 ("Metronom") };

    /** Enables/disables loop playback; lit when transport.looping is true. */
    IconButton loopButton { AizanaIcon::Loop, juce::String::fromUTF8 ("Schleife") };

    /** Tap Tempo — average of last 4 taps; >2s gap resets sequence. */
    IconButton tapTempoButton { AizanaIcon::Tap, juce::String::fromUTF8 ("Tempo tippen") };

    /** Metronome settings ▾ — opens a popover bundling Vorzähler (count-in),
     *  Pre-Roll, Downbeat accent and click level (Sketch 006 A, Studio One pattern).
     *  Sits right of the metronome toggle as a split-button affordance. */
    IconButton metroMenuButton { AizanaIcon::Chevron, juce::String::fromUTF8 ("Metronom-Einstellungen") };
    void showMetronomeMenu();

    /** Auto-Punch — icon toggle; recording only happens within the Loop In/Out
     *  markers (te::Edit::recordingPunchInOut, persisted). Red when armed. */
    IconButton autoPunchButton { AizanaIcon::Punch, juce::String::fromUTF8 ("Auto-Punch") };
    void toggleAutoPunch();
    void updateAutoPunchButton();

    /** Pre-Roll bars (0/1/2): roll the transport back N bars before the record
     *  start for an audible run-up. Set via the metronome popover; applied in
     *  recordClicked(). Transient. Pairs with Auto-Punch (run-up not recorded). */
    int  preRollBars = 0;

    // ---- Vorzaehler (Count-in) ---------------------------------------------
    //
    // Takte Vorlauf: 0 = aus, sonst 1 / 2 / 4. Der Ablauf selbst steht in
    // `CountInRecorder` (src/engine/CountIn.h) — dort ist auch begruendet,
    // warum er nahtlos ist und warum die beiden Vorgaenger-Anlaeufe scheiterten.
    //
    // Vorzaehler und Pre-Roll sind DERSELBE Vorlauf; sie unterscheiden sich
    // ausschliesslich darin, ob der Klick fuer die Dauer erzwungen wird
    // (Logics „Only during count-in"). Zwei Mechaniken dafuer zu bauen waere
    // zweimal dieselbe Fehlerquelle.
    int  countInBars = 0;                 // 0 = aus, sonst 1 / 2 / 4

    CountInRecorder countIn;

    /** The Dial (Phase 2, Block A) — switch to the Einsteiger-Welt + assist toggle.
     *  Only visible in the Pro view, so the mode button always means "go to Beginner". */
    juce::TextButton modeButton   { "Einsteiger" };
    juce::TextButton assistButton { "Assist" };

    /** Edit/Mix segmented pair — see onSelectViewMode. */
    juce::TextButton editViewButton { "Edit" };
    juce::TextButton mixViewButton  { "Mix" };

    /** KI-Mix co-pilot — heuristic mix in one click (see onMixRequested). */
    juce::TextButton kiMixButton { juce::String::fromUTF8 ("KI-Mix") };

    /** KI-Studio — opens the in-app AI assistant chat (see onAiStudioRequested). */
    juce::TextButton kiStudioButton { juce::String::fromUTF8 ("KI-Studio") };

    // Tap tempo state — last 4 tap timestamps (juce::Time::getMillisecondCounterHiRes)
    juce::Array<double> tapTimes;

    void tapTempoClicked();

    /** Shows current playback position — e.g. "0:00:00.000". Updated every 30ms. */
    juce::Label positionLabel;

    /** Shows the edit BPM — e.g. "120 BPM". Double-click to edit. */
    juce::Label bpmLabel;

    /** Time signature — single clean "4/4" button; click opens a list of common
     *  signatures (Studio One style, replaces the two empty-looking combos). */
    juce::TextButton timeSigButton { "4/4" };
    void showTimeSigMenu();
    void applyTimeSig (int numerator, int denominator);

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
