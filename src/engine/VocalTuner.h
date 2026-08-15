#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "EditManager.h"
#include "PitchDetector.h"

namespace te = tracktion::engine;

/**
 * VocalTuner — P8 Vocal-Tuning (S1/Fender-8.1-Äquivalent, v1 offline).
 *
 * Weg: Pitch-Kurve (PitchDetector/YIN) → Ziel = nächster Skalenton →
 * geglättete Korrekturkurve (Cents, Stärke skalierbar) → SoundTouch-
 * Pitch-Shift blockweise über die Datei (Zeit bleibt 1:1) → gerenderte
 * Datei ersetzt den Clip in place (Bounce-Muster, EIN Undo-Schritt).
 *
 * Kein ARA, kein Realtime — bewusst der Bounce-Weg: headless beweisbar
 * (Render-Test), kein Audio-Thread-Risiko, Undo trivial. Der „KI-Weg"
 * (automatische Key-Erkennung, notenweises Editieren) baut später darauf auf.
 *
 * Die reinen Kerne (hzToMidi/snapMidiToScale/buildCorrectionSemitones/
 * renderCorrected) sind statisch und golden-testbar; tuneClipInPlace
 * öffnet als einziges eine Undo-Transaktion (UI-Konvention wie ClipEditor).
 */
class VocalTuner
{
public:
    enum class Scale { chromatic, major, minor };

    struct Settings
    {
        int   keyRootNote = 0;       // Pitch-Class 0=C … 11=B (für major/minor)
        Scale scale       = Scale::chromatic;
        float strength    = 1.0f;    // 0..1 — Anteil der Korrektur (1 = voll auf den Ton)
        float smoothingMs = 35.0f;   // Glättung der Korrekturkurve („Speed“ — klein = härter)
    };

    explicit VocalTuner (EditManager& em) : editManager (em) {}

    // ---- Reine Kerne (golden-testbar) ----

    static double hzToMidi (double hz);
    static double midiToHz (double midi);

    /** Nächster Ton der Skala (MIDI, ganzzahlig). Chromatisch = runden;
        Dur/Moll = nächstliegender Skalenton (Gleichstand → abwärts). */
    static int snapMidiToScale (double midi, int keyRootNote, Scale scale);

    /**
     * Korrekturkurve in Halbtönen (ein Wert pro Pitch-Frame).
     * Unvoiced-Frames halten die letzte Korrektur (keine Zips an Konsonanten);
     * anschließend One-Pole-Glättung mit smoothingMs bei frameRate (Frames/s).
     */
    static juce::Array<float> buildCorrectionSemitones (const juce::Array<float>& pitchHz,
                                                        const Settings& settings,
                                                        double frameRate);

    /**
     * Wendet die Korrekturkurve per SoundTouch-Pitch-Shift auf den Buffer an
     * (Tempo 1:1, nur Pitch; Shift wird pro Hop-Block nachgeführt).
     * out wird auf die Länge von in gebracht. false bei Init-Fehler.
     */
    static bool renderCorrected (const juce::AudioBuffer<float>& in,
                                 juce::AudioBuffer<float>& out,
                                 double sampleRate,
                                 const juce::Array<float>& correctionSemitones,
                                 int hopSize);

    // ---- Materialprüfung (C3) ------------------------------------------------
    //
    // Warum das existiert: die alte Sammelmeldung „Kein tonales Material erkannt
    // oder Clip nicht tunebar (gestretcht/zu kurz)" nannte VIER verschiedene
    // Ursachen auf einmal — und erst NACH dem Rechnen. Als der User damit einen
    // Flöten-Stem abgewiesen bekam, war nicht feststellbar, welche davon zutraf;
    // die naheliegende Vermutung (Tonumfang zu eng) hat sich in der Nachmessung
    // als FALSCH erwiesen (der Detektor findet die Flöte bei 358 Hz, 44 %
    // stimmhafte Frames, und ein weiterer Suchbereich ändert daran nichts —
    // Messreihe 2026-08-15, "[pitchrange]").
    //
    // Also der Melodyne-Weg: Material wird VORHER vermessen und das Ergebnis
    // gezeigt; abgelehnt wird nur, was wirklich unmöglich ist. Zweifelhaftes
    // Material wird benannt, aber nicht verboten — Melodyne verweigert nie die
    // Analyse, es lässt den Anwender entscheiden.
    enum class Status
    {
        ok,                 // tunebar
        wrongEdit,          // Clip gehört nicht zu diesem Edit (Programmfehler)
        noTrack,
        stretched,          // speedRatio ≠ 1 oder autoTempo → Datei-Zeit ≠ Edit-Zeit
        emptyClip,
        unreadableSource,   // Quelldatei fehlt/nicht lesbar (z. B. Warp-Proxy noch nicht da)
        tooShort,           // < 4096 Samples im gespielten Ausschnitt
        notTonal,           // kein einziger stimmhafter Frame
        renderFailed,       // SoundTouch-Init fehlgeschlagen
        writeFailed         // Zieldatei nicht schreibbar
    };

    struct Analysis
    {
        Status status      = Status::ok;
        double voicedRatio = 0.0;   // Anteil stimmhafter Frames (0..1)
        double medianHz    = 0.0;   // Median-Grundton der stimmhaften Frames
        double lengthSecs  = 0.0;   // vom Clip gespielte Länge

        bool isTunable() const noexcept { return status == Status::ok; }

        /** Eine Zeile Klartext für den Dialog — nennt die EINE zutreffende Ursache
            bzw. bei Erfolg das gemessene Material. */
        juce::String describe() const;
    };

    /** Unterhalb dieses Anteils stimmhafter Frames ist das Ergebnis zweifelhaft.
        Aus echtem Material abgeleitet (Messreihe 2026-08-15, "[pitchrange]",
        lauteste 10 s): 808-Bass 8,7 % · Akkorde 25,9 % · Flöte 44,1 % ·
        Gesang 48,7–53,3 % · Kalimba 68,1 %. 0,30 trennt „klar tonal, monofon
        genug" von „daraus wird nichts Gutes" mit Abstand nach beiden Seiten. */
    static constexpr double kDoubtfulVoicedRatio = 0.30;

    /** Prüft und VERMISST den Clip, ohne etwas zu verändern. Blockierend
        (liest + analysiert die Datei) → für lange Clips auf einen BG-Thread. */
    Analysis analyseClip (te::WaveAudioClip& clip) const;

    // ---- Clip-Workflow (EIN Undo-Schritt) ----

    /**
     * Liest den vom Clip gespielten Datei-Ausschnitt (aktuelle Quelle — bei
     * aktivem Warp also die gewarpte Datei), korrigiert ihn und ersetzt den
     * Clip durch die getunte Datei an derselben Position („… (Tuned)").
     * Synchron (für lange Clips vom Aufrufer in einen BG-Task legen).
     * nullptr bei jedem Status ≠ ok; die Ursache landet in *outAnalysis,
     * wenn übergeben (kein zweiter Lesedurchgang nötig).
     */
    te::WaveAudioClip* tuneClipInPlace (te::WaveAudioClip& clip, const Settings& settings,
                                        Analysis* outAnalysis = nullptr);

private:
    /** Gemeinsamer Vorlauf von analyseClip und tuneClipInPlace: Guards prüfen,
        Ausschnitt lesen, Pitch-Kurve rechnen. Genau EIN Lesedurchgang. */
    struct Prepared
    {
        Analysis analysis;
        juce::AudioBuffer<float> buffer;   // gespielter Ausschnitt, Originalkanäle
        juce::Array<float>       pitchHz;
        double rate    = 0.0;
        int    hopSize = 0;
    };

    Prepared prepare (te::WaveAudioClip& clip) const;

    EditManager& editManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalTuner)
};
