#pragma once

#include "EditManager.h"

namespace te = tracktion::engine;

/**
 * TempoEngine (T1.2) — Tempo- und Taktartwechsel auf TEs TempoSequence.
 *
 * TE kann Tempo-Kurven seit jeher (TempoSequence/TempoSetting/TimeSigSetting,
 * alles ValueTree-basiert = Undo/Persistenz gratis) — Aizana hatte nur keine UI
 * und damit faktisch EIN Tempo pro Song. Dieser Service ist die dünne, testbare
 * Mutations-Schicht darunter; die TempoLane ist reine View/Interaktion.
 *
 * Alle Mutationen = eine Undo-Transaktion pro User-Geste (I-02).
 * remapEdit=true beim Einfügen/Löschen: Clips behalten ihre BEAT-Position —
 * das ist das musikalisch richtige Verhalten (Studio One "Timebase: Beats").
 */
class TempoEngine
{
public:
    explicit TempoEngine (EditManager& editManager);

    struct TempoPoint
    {
        int    index   = 0;      // Index in der TempoSequence
        double timeSec = 0.0;    // Startzeit in Sekunden (abgeleitet)
        double beat    = 0.0;    // Startposition in Beats (Quelle der Wahrheit)
        double bpm     = 120.0;
    };

    struct TimeSigPoint
    {
        int    index   = 0;
        double timeSec = 0.0;
        double beat    = 0.0;
        int    numerator   = 4;
        int    denominator = 4;
    };

    // ---- Queries (Message-Thread) -------------------------------------------
    juce::Array<TempoPoint>   getTempoPoints()   const;
    juce::Array<TimeSigPoint> getTimeSigPoints() const;

    // ---- Gesten (je 1 Undo-Schritt) ------------------------------------------

    /** Tempowechsel an der Zeitposition einfügen (auf ganzen Beat gerundet).
        bpm ≤ 0 → übernimmt das dort geltende Tempo. Liefert den neuen Index oder -1. */
    int addTempoChange (double timeSec, double bpm = 0.0);

    /** BPM eines bestehenden Punkts setzen (geclampt auf TE-Limits 20..300).
        beginTransaction=false lässt eine Drag-Geste in EINEN Undo-Schritt falten. */
    void setTempoBpm (int index, double bpm, bool beginTransaction = true);

    /** Punkt entfernen. Der erste Punkt (Song-Grundtempo) ist nicht löschbar. */
    void removeTempoChange (int index);

    /** Taktartwechsel am nächsten Taktanfang zur Zeitposition einfügen. */
    int addTimeSigChange (double timeSec, int numerator, int denominator);

    /** Taktart eines bestehenden Punkts ändern. */
    void setTimeSig (int index, int numerator, int denominator);

    /** Taktart-Punkt entfernen (Index 0 nicht löschbar). */
    void removeTimeSigChange (int index);

    // ---- P7a: Tempo-Map-Extraktion (S1 "Extract to tempo track") -------------

    /**
     * Ersetzt die Tempo-Map so, dass das GRID exakt auf den übergebenen
     * Beat-Zeiten landet — das Audio bleibt stehen (kein Remap; Clip-Positionen
     * sind zeitverankert, nur das Raster verschiebt sich).
     *
     * Mechanik: pro Beat-Intervall ein Tempo-STEP (curve=1.0 hält die BPM bis
     * zum nächsten Punkt) mit bpm = 60/Δt an ganzzahligen Beat-Positionen;
     * liegt der erste Beat nicht bei 0, bekommt der Vorlauf ein Tempo, das eine
     * ganze Beat-Anzahl exakt auf beatTimes[0] enden lässt. Konstante Läufe
     * werden zu EINEM Punkt zusammengefasst. TE-Clamp 20..300 BPM.
     *
     * beatTimesEditSecs: aufsteigend, EDIT-Zeit, >= 2 Einträge.
     * EIN Undo-Schritt. Liefert die Anzahl gesetzter Tempo-Punkte (0 = nichts).
     */
    int extractTempoMap (const juce::Array<double>& beatTimesEditSecs);

    /**
     * Convenience: Beat-Zeiten aus der QUELLDATEI eines Clips (TempoDetector-
     * Ausgabe) → Edit-Zeit (clipStart + t − offset), auf den Clip-Bereich
     * gefiltert, dann extractTempoMap. Clips mit autoTempo (Timestretch) werden
     * abgelehnt (Datei-Zeit ≠ Edit-Zeit). Liefert die Anzahl Tempo-Punkte.
     */
    int extractTempoMapFromClip (te::WaveAudioClip& clip,
                                 const juce::Array<double>& beatTimesFileSecs);

private:
    EditManager& editManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoEngine)
};
