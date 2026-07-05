#pragma once

/**
 * EditorViewState — pure Sicht-Fenster-Mathe für den Docked Editor
 * (AudioClipEditor). Bewusst OHNE JUCE-/TE-Abhängigkeit → golden-testbar
 * (tests/test_editor_view_state.cpp).
 *
 * S1-Anker-Prinzip (Studio-One-Parität): die Zeitachse des Editors steht FEST.
 * Clip-Mutationen (Split, Range-Delete, Trim) und Selektionswechsel verschieben
 * das Sichtfenster NICHT — nur explizite User-Aktionen (Zoom, Scroll, Fit).
 * Deshalb darf das Fenster über die Clip-Grenzen hinausragen (Überhang), statt
 * nach jedem Schnitt auf die neue Clip-Länge zusammenzuschnappen (der
 * "Editor springt nach dem Cut"-Befund vom 2026-07-06).
 */
namespace editorview
{
    struct Window
    {
        double startLocal = 0.0;  // clip-LOKALE Sekunden; darf < 0 sein (vor Clip-Start)
        double len        = 0.0;  // Fensterlänge in Sekunden
    };

    constexpr double kMinLen = 0.02;

    /** Fenster von einem Clip-Ursprung auf einen anderen umrechnen —
        die ABSOLUTE Zeitlage des Fensters bleibt identisch. */
    inline Window rebase (Window w, double oldClipStartAbs, double newClipStartAbs) noexcept
    {
        return { w.startLocal + (oldClipStartAbs - newClipStartAbs), w.len };
    }

    /** Fenster nutzbar machen: Mindestlänge erzwingen; das Fenster MUSS den
        Clip [0, clipLen] schneiden. Kein Schnitt (z.B. Handover auf einen weit
        entfernten Clip) → Fit auf den ganzen Clip. */
    inline Window ensureUsable (Window w, double clipLen) noexcept
    {
        if (clipLen <= 0.0)
            return { 0.0, kMinLen };

        if (w.len < kMinLen)
            w.len = kMinLen;

        const bool intersects = w.startLocal < clipLen
                             && (w.startLocal + w.len) > 0.0;
        if (! intersects)
            return { 0.0, clipLen };   // Fit

        return w;
    }

    /** Zoom-Grenze: nie weiter raus als max(Cliplänge, aktuelles Fenster).
        Erhält den Überhang nach Splits, verhindert aber unbegrenztes Auszoomen. */
    inline double clampZoomLen (double requestedLen, double clipLen, double currentLen) noexcept
    {
        double maxLen = clipLen > currentLen ? clipLen : currentLen;
        if (maxLen < kMinLen)
            maxLen = kMinLen;

        if (requestedLen < kMinLen) return kMinLen;
        if (requestedLen > maxLen)  return maxLen;
        return requestedLen;
    }
}
