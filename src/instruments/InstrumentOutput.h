#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// InstrumentOutput.h — gemeinsame Ausgangsstufe der Built-in-Instrumente.
//
// HINTERGRUND (Messung 2026-08-10, Live-Testlauf-Vorbereitung):
// Die Instrumente lieferten mit Werkseinstellungen Pegel WEIT ueber Vollaussteuerung:
//
//     Drums  Kick  1.458 | Snare 1.043 | Rim 1.168 | Toms 0.845 | Hats/Clap ~0.46
//     Synth  1 Note 0.52-0.75  ->  4 Noten 1.38-2.52
//
// Eine einzelne Kick-Note bei Velocity 1.0 stand damit +3,2 dB ueber Vollaussteuerung —
// ein frisch geladenes Instrument verzerrte beim ERSTEN Ton, ohne Zutun des Users.
// Kein Test fing das, weil alle Instrument-Tests nur Untergrenzen pruefen ("nicht still").
//
// ZWEI MASSNAHMEN, bewusst getrennt:
//
//  1. Kalibrierungs-Trim auf die SYNTHETISIERTEN Stimmen. Ein einziger Faktor pro
//     Instrument, damit die INTERNE Balance (Hats leiser als Kick usw.) exakt so
//     bleibt, wie sie entworfen wurde — es wird nur die ganze Kette leiser gemacht.
//     Vom User geladene Samples bleiben unangetastet: fremdes Material ist bereits
//     normalisiert, da waere ein zusaetzlicher Trim eine Bevormundung.
//
//  2. Weiche Begrenzung auf dem Summenausgang. Sie faengt ab, was Punkt 1
//     konstruktionsbedingt nicht kann: das lineare Summieren gleichzeitiger Stimmen
//     (4-Noten-Akkord = 4x eine Note) und laute User-Samples. Statt hart zu clippen,
//     legt sich die Summe asymptotisch an 1.0 an.
//
// Beides ist reine Arithmetik: keine Allokation, kein Lock, kein Zweig ueber Daten —
// audio-thread-sicher (I-01).
namespace aizana::instrument
{
    // Kick war mit 1.458 die lauteste Stimme des Kits. Ziel: -6 dBFS (0.5) bei
    // Velocity 1.0, damit ein dichter Beat (mehrere Stimmen gleichzeitig) unter
    // Vollaussteuerung bleibt. 0.5 / 1.458 = 0.343.
    inline constexpr float kDrumVoiceTrim = 0.343f;

    // Eine Synth-Note lag bei ~0.65. Polyphone Instrumente muessen den Akkord
    // einkalkulieren, nicht die Einzelnote: mit 0.35 landet ein Vierklang bei
    // ~0.88 statt bei 2.5, eine Einzelnote bei ~0.23 (-13 dBFS). Das entspricht
    // dem, was Werkspatches kommerzieller Synths ausgeben.
    inline constexpr float kSynthVoiceTrim = 0.35f;

    // Weiche Begrenzung: unterhalb der Schwelle voellig transparent (identisch,
    // Bit fuer Bit), darueber rational gegen 1.0 laufend. Erreicht 1.0 nie, kann
    // also per Konstruktion nicht clippen.
    inline float softClip (float x) noexcept
    {
        constexpr float threshold = 0.70f;
        constexpr float range     = 1.0f - threshold;

        const float mag = std::abs (x);
        if (mag <= threshold)
            return x;

        const float over    = mag - threshold;
        const float limited = threshold + range * (over / (over + range));
        return std::copysign (limited, x);
    }

    // Auf den fertigen Instrumentenausgang anwenden (letzte Zeile von processBlock).
    inline void softClipBuffer (juce::AudioBuffer<float>& buffer) noexcept
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                d[i] = softClip (d[i]);
        }
    }
}
