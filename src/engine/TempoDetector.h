#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * TempoDetector — offline BPM estimation for an audio file (Phase 2+,
 * Studio One "Tempo-Detection" gap; simple DSP now, Essentia comes in Phase 3).
 *
 * Method: onset-strength envelope (rectified frame-energy flux, hop 512)
 * → autocorrelation over the lag range 60–200 BPM → peak pick → octave fold
 * into [70, 180). Works well on rhythmic material; it is an ESTIMATE and the
 * UI labels it as such.
 *
 * Threading: detectBpmInFile is pure and blocking — call it on a background
 * thread (the UI uses detectAsync, result delivered via callAsync). Never call
 * on the audio thread.
 */
class TempoDetector
{
public:
    /** Blocking analysis. Returns estimated BPM, or 0.0 when undetectable
        (silence, too short, unreadable file). Analyses at most the first 60 s. */
    static double detectBpmInFile (const juce::File& audioFile);

    /** Same analysis on an in-memory mono buffer (used by tests). */
    static double detectBpm (const juce::AudioBuffer<float>& mono, double sampleRate);

    /** Background-thread wrapper: runs detectBpmInFile on a detached worker and
        delivers the result on the message thread. The callback always fires
        (0.0 on failure) unless the app is shutting down. */
    static void detectAsync (const juce::File& audioFile,
                             std::function<void (double bpm)> onResult);

    // -------------------------------------------------------------------------
    // P7a: Beat-Tracking (Tempo-Map-Extraktion, S1 "Extract to tempo track")
    // -------------------------------------------------------------------------

    /**
     * Beat-Zeiten PLUS Verlässlichkeitsmaß.
     *
     * Warum das existiert (C2, User-Befund 2026-08-13): auf einem Kick-Stem
     * allein lieferte der Tracker drei Beats mit 81/94/99 BPM — überzeugt
     * falsch, ohne jeden Hinweis. Genau diese Klasse ist ein Vertrauensproblem:
     * ein falsches Ergebnis, das wie ein Messergebnis aussieht.
     *
     * Der Industrieweg ist ABLEHNEN statt raten: Studio One meldet, dass es
     * „kein realistisches Tempo bestimmen kann, wenn es zu wenig Cadence gibt";
     * Cubase verlangt mindestens 7 s Material mit erkennbaren Beats und meldet,
     * wenn es nicht weiterkommt. Aizana misst dafür zwei unabhängige Größen:
     *
     *  - onsetSupport       Anteil der Beats, unter denen wirklich ein Anschlag
     *                       liegt (rektifizierte Onset-Stärke in SD-Einheiten
     *                       ≥ kOnsetSupportSd im Fenster ±1 Frame). Sparsames
     *                       Material (Kick allein) lässt den DP ins Leere
     *                       schlagen — genau das misst diese Zahl.
     *  - intervalConsistency Anteil der Beat-Abstände innerhalb ±15 % des
     *                       Medians. Fängt den Fall „Puls verloren, Raster
     *                       eiert" auch dann, wenn zufällig Onsets darunter
     *                       liegen.
     *
     * confidence = min(beider) — eine Kette ist nur so gut wie ihr schwächstes
     * Glied, und beide Fehlerbilder sind unabhängig voneinander möglich.
     */
    struct BeatResult
    {
        juce::Array<double> beats;
        double onsetSupport        = 0.0;   // 0..1
        double intervalConsistency = 0.0;   // 0..1
        double confidence          = 0.0;   // min(beider), 0..1

        bool isEmpty() const noexcept { return beats.size() < 2; }
    };

    /** Schwelle, ab der eine Tempo-Map angeboten wird. Aus echtem Material
        abgeleitet (Messreihe 2026-08-15, siehe test_tempo_detector.cpp
        „[confidence]"): Drum-Summe/Vollmix liegen deutlich darüber, ein
        Kick-Stem allein deutlich darunter. */
    static constexpr double kMinConfidence = 0.60;

    /** Wie detectBeats, zusätzlich mit den Verlässlichkeitsmaßen. */
    static BeatResult detectBeatsWithConfidence (const juce::AudioBuffer<float>& mono,
                                                 double sampleRate);

    /** Wie oben, auf den ersten 60 s einer Datei. */
    static BeatResult detectBeatsWithConfidenceInFile (const juce::File& audioFile);

    /** BG-Thread-Wrapper; Callback bekommt immer ein Ergebnis (ggf. leer). */
    static void detectBeatsWithConfidenceAsync (const juce::File& audioFile,
                                                std::function<void (BeatResult)> onResult);

    /**
     * Beat-Zeitpunkte in Sekunden (Puffer-/Datei-Zeit), aufsteigend.
     *
     * Verfahren: dieselbe Onset-Envelope wie detectBpm + Ellis-2007-DP —
     * Score(t) = Onset(t) + max über Vorgänger im Fenster [τ/2, 2τ] von
     * (Score(prev) − α·log²(Δ/τ)), Backtrack vom besten Endpunkt. Folgt
     * moderaten Tempo-Schwankungen (die Strafe wirkt auf das Verhältnis zum
     * globalen Puls, nicht auf absolute Positionen).
     *
     * Leer wenn kein stabiler Puls erkennbar. Rein + blockierend → BG-Thread.
     */
    static juce::Array<double> detectBeats (const juce::AudioBuffer<float>& mono,
                                            double sampleRate);

    /** Wie detectBeats, auf den ersten 60 s einer Datei (Mono-Mixdown). */
    static juce::Array<double> detectBeatsInFile (const juce::File& audioFile);

    /** BG-Thread-Wrapper wie detectAsync; Callback liefert ggf. ein leeres Array. */
    static void detectBeatsAsync (const juce::File& audioFile,
                                  std::function<void (juce::Array<double>)> onResult);
};
