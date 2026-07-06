#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/**
 * PitchDetector — monophone Tonhöhen-Erkennung (YIN, offline).
 *
 * Grundlage für P8 Vocal-Tuning: liefert eine Pitch-Kurve (Hz pro Hop-Frame)
 * über der Aufnahme; 0 = unvoiced (Stille/Konsonant/kein klarer Ton).
 *
 * Rein und deterministisch — golden-testbar (Sinus → f0). Kein Audio-Thread-
 * Code: Analyse läuft offline auf dem Message-/BG-Thread (P8-Bounce-Weg).
 *
 * YIN (de Cheveigné/Kawahara 2002): Differenzfunktion + kumulativ normierte
 * CMNDF, erste Unterschreitung des Schwellwerts, parabolische Interpolation.
 */
class PitchDetector
{
public:
    struct Settings
    {
        int   windowSize = 2048;    // Analysefenster (Samples)
        int   hopSize    = 512;     // Frame-Vorschub → Kurven-Rate = rate/hop
        float minHz      = 60.0f;   // tiefste erwartete Stimme
        float maxHz      = 1000.0f; // höchste erwartete Stimme
        float threshold  = 0.15f;   // CMNDF-Schwelle (kleiner = strenger voiced)
    };

    /** Pitch-Kurve in Hz, ein Wert pro Hop-Frame; 0 = unvoiced. */
    static juce::Array<float> detectPitchHz (const float* mono, int numSamples,
                                             double sampleRate, Settings s);

    /** Wie oben mit Default-Settings (Default-Argument in-class ist wegen der
        NSDMI-Parse-Reihenfolge nicht erlaubt → Overload). */
    static juce::Array<float> detectPitchHz (const float* mono, int numSamples,
                                             double sampleRate);

    /** Median der voiced Frames (0 wenn keiner voiced). Praktisch für Tests. */
    static float medianPitchHz (const juce::Array<float>& curve);

private:
    PitchDetector() = delete;
};
