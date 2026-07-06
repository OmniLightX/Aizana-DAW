#pragma once

#include <juce_core/juce_core.h>

/**
 * PanValue (UX4) — pures Formatieren/Parsen von Pan-Werten (golden-testbar).
 *
 * Format: "C" (Mitte), "L1".."L100", "R1".."R100" — Studio-One-Konvention.
 * Parser akzeptiert zusätzlich nackte Zahlen (-100..100, negativ = links)
 * und "0" als Mitte; case-insensitiv, Whitespace-tolerant.
 */
namespace aizana::panvalue
{
    /** -1..1 → "L33" / "C" / "R48". */
    inline juce::String format (double pan)
    {
        const int pct = juce::roundToInt (std::abs (pan) * 100.0);
        if (pct == 0) return "C";
        return (pan < 0.0 ? "L" : "R") + juce::String (pct);
    }

    /** "L45"/"r30"/"C"/"-20"/"75" → -1..1. Liefert false bei Unparsbarem. */
    inline bool parse (const juce::String& textIn, double& outPan)
    {
        const auto text = textIn.trim().toUpperCase();
        if (text.isEmpty())
            return false;

        if (text == "C" || text == "0")
        {
            outPan = 0.0;
            return true;
        }

        const auto first = text[0];
        if (first == 'L' || first == 'R')
        {
            const auto rest = text.substring (1).trim();
            if (rest.isEmpty() || ! rest.containsOnly ("0123456789"))
                return false;
            const int pct = juce::jlimit (0, 100, rest.getIntValue());
            outPan = (first == 'L' ? -1.0 : 1.0) * pct / 100.0;
            return true;
        }

        // Nackte Zahl: -100..100 (negativ = links).
        if (! text.containsOnly ("0123456789+-"))
            return false;
        const int pct = juce::jlimit (-100, 100, text.getIntValue());
        outPan = pct / 100.0;
        return true;
    }
}
