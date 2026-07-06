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

    // ---- Clip-Workflow (EIN Undo-Schritt) ----

    /**
     * Liest den vom Clip gespielten Datei-Ausschnitt (aktuelle Quelle — bei
     * aktivem Warp also die gewarpte Datei), korrigiert ihn und ersetzt den
     * Clip durch die getunte Datei an derselben Position („… (Tuned)").
     * Synchron (für lange Clips vom Aufrufer in einen BG-Task legen).
     * nullptr bei speedRatio ≠ 1, unlesbarer Quelle oder leerem Clip.
     */
    te::WaveAudioClip* tuneClipInPlace (te::WaveAudioClip& clip, const Settings& settings);

private:
    EditManager& editManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalTuner)
};
