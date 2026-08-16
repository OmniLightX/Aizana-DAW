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

    // ---- Vorzaehler (Count-in) — eigener Zustand, NICHT TEs countInMode ------
    //
    // User-Befund 2026-08-16: "Der Vorzaehler klackt einfach die ganze Zeit und
    // faengt nicht an." Zwei Ursachen, beide belegt:
    //
    //  1. ZWEI KLICKQUELLEN. Unser MetronomeGenerator ist von TEs eigenem Click
    //     unabhaengig und klickt durchgehend, solange er an ist — auch waehrend
    //     und nach dem Vorzaehler.
    //  2. TEs `countInMode` spannt in TransportControl.cpp:1521 eine Schleife
    //     von der Aufnahmeposition bis zum Songende auf und rollt per
    //     `setRollInToLoop` hinein. Steht danach die BENUTZER-Schleife (der
    //     Block direkt darunter ueberschreibt sie), rollt der Kopf in DIESE
    //     hinein — und laeuft dort im Kreis, ohne je den Aufnahmestart zu
    //     erreichen. Genau das Beschriebene.
    //
    // Deshalb bauen wir den Vorzaehler selbst, mit unserer einen Klickquelle,
    // nach dem Vorbild der Referenz:
    //   Studio One: "a count-off of the length indicated in the adjacent Bars
    //   field occurs when the Record button is clicked" — er gehoert zur
    //   AUFNAHME, nicht zur Wiedergabe, und ist von Pre-Roll getrennt.
    //   Logic: "playback begins before the playhead position. When reaching the
    //   playhead position, Logic switches to record mode." Dazu die Option
    //   "Only during count-in" fuer den Klick.
    //
    // Umsetzung: Wiedergabe beginnt N Takte frueher, das Metronom wird fuer die
    // Dauer des Vorzaehlers ERZWUNGEN (und danach auf seinen alten Stand
    // zurueckgesetzt), und aufgenommen wird ab dem eigentlichen Startpunkt —
    // ueber TEs Punch-In, damit der Vorlauf NICHT im Clip landet.
    int  countInBars = 0;                 // 0 = aus, sonst 1 / 2 / 4

    bool countInActive       = false;     // laeuft gerade ein Vorzaehler?
    bool metronomeWasEnabled = false;     // Stand vor dem Erzwingen
    bool punchWasEnabled     = false;     // Stand von recordingPunchInOut
    tracktion::TimeRange savedLoopRange;  // Benutzer-Schleife, wird wiederhergestellt
    bool loopWasEnabled      = false;

    /** Startet die Aufnahme mit Vorzaehler. Getrennt von recordClicked, damit
        der Ablauf an einer Stelle steht und testbar bleibt. */
    void startRecordingWithCountIn();

    /** Raeumt auf, was der Vorzaehler umgestellt hat (Metronom, Punch, Loop). */
    void finishCountIn();

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
