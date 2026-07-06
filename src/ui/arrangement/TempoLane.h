#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/TempoEngine.h"

namespace te = tracktion::engine;

/**
    TempoLane (T1.2) — schmale Leiste über dem Ruler (unter der MarkerLane),
    die die Tempo-Kurve des Songs zeigt und editierbar macht (Studio One
    "Tempo Track"). Rein View/Interaktion — Zustand lebt in TEs TempoSequence
    (ValueTree → Undo/Persistenz gratis), Mutationen laufen über TempoEngine.

    Interaktion:
      • Doppelklick leer        → Tempowechsel an der Position (übernimmt BPM dort)
      • Punkt vertikal ziehen   → BPM ändern (1 Undo pro Geste); Shift = fein
      • Rechtsklick Punkt       → BPM eingeben… / Löschen (Punkt 0 nicht löschbar)
      • Rechtsklick leer        → Tempowechsel einfügen / Taktart hier ändern…
      • Taktart-Badges (4/4)    → Rechtsklick: ändern/löschen

    Zeit→x teilt pixelsPerSecond/scrollPositionSecs mit dem Ruler (by-ref) —
    Lane bleibt pixel-deckungsgleich mit Ruler und Clips.
*/
class TempoLane : public juce::Component
{
public:
    TempoLane (ApplicationState& appState,
               TempoEngine& tempoEngine,
               double& pixelsPerSecond,
               double& scrollPositionSecs);

    void paint (juce::Graphics& g) override;

    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    juce::MouseCursor getMouseCursor() override;

    static constexpr int kLaneHeight = 26;

private:
    te::Edit* getEdit() const;

    double xToTime (int x)      const;
    int    timeToX (double sec) const;
    float  bpmToY  (double bpm) const;   // log-nah gemappt in [kBpmMin..kBpmMax]
    double yToBpm  (float y)    const;

    int tempoPointAtX (int x) const;     // Index in getTempoPoints() oder -1
    int timeSigBadgeAtX (int x) const;   // Index in getTimeSigPoints() oder -1

    void showTempoPointMenu (int index);
    void showEmptyAreaMenu (double timeSec);
    void showTimeSigMenu (int index);
    void promptBpm (int index);

    ApplicationState& appState;
    TempoEngine&      tempoEngine;
    double& pixelsPerSecond;
    double& scrollPositionSecs;

    static constexpr double kBpmMin = 40.0, kBpmMax = 240.0;

    // Drag: BPM des gegriffenen Punkts ändern; erste Bewegung öffnet die Transaktion.
    int    dragIndex     = -1;
    double dragStartBpm  = 0.0;
    float  dragStartY    = 0.0f;
    bool   dragTxnOpen   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoLane)
};
