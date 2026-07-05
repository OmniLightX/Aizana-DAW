// test_editor_view_state.cpp — Fix-Welle 2026-07-06 (Docked-Editor-Befunde):
//
//   1) S1-VIEW-ANKER: Nach Split/Range-Delete stand die Edit-View nicht fest —
//      clampView() schnappte das Fenster auf die neue (kürzere) Clip-Länge und
//      das Bild sprang. Die pure Fenster-Mathe (editorview::*) hält die
//      absolute Zeitlage jetzt fest (Überhang erlaubt, Fit nur ohne Schnitt).
//
//   2) AUTOMATION-ZEITMAPPING: Die AutomationLane rechnete im Docked Editor
//      x=0 als t=0 (Arrangement-Konvention) — Punkte landeten um
//      (Clip-Start + View-Scroll) ZU FRÜH im Song und waren am Clip unhörbar.
//      Das Viewport-Mapping (laneXToTime/laneTimeToX mit origin) fixt das.
//
// Alles pure Funktionen — headless, golden-testbar.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/dockeditor/EditorViewState.h"
#include "ui/automation/AutomationLane.h"

using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// editorview::rebase — absolute Fensterlage bleibt beim Ursprungs-Wechsel
// ---------------------------------------------------------------------------

TEST_CASE ("EditorView: rebase keeps the absolute window position", "[editorview][dock]")
{
    // Fenster beginnt 2s nach Clip-Start; Clip liegt bei 30s absolut.
    const editorview::Window w { 2.0, 10.0 };

    // Selektionswechsel auf das rechte Split-Teilstück bei 35s absolut:
    const auto r = editorview::rebase (w, 30.0, 35.0);

    // Absolut: 30+2 = 32s — relativ zum neuen Ursprung 35s → lokal -3s.
    CHECK_THAT (r.startLocal, WithinAbs (-3.0, 1e-9));
    CHECK_THAT (r.len,        WithinAbs (10.0, 1e-9));

    // Identischer Ursprung = identisches Fenster (Split: linkes Stück behält Start)
    const auto same = editorview::rebase (w, 30.0, 30.0);
    CHECK_THAT (same.startLocal, WithinAbs (2.0, 1e-9));
}

// ---------------------------------------------------------------------------
// editorview::ensureUsable — der eigentliche "Editor springt nach Cut"-Fix
// ---------------------------------------------------------------------------

TEST_CASE ("EditorView: window with overhang past the clip end is PRESERVED", "[editorview][dock]")
{
    // Der Atmer-Workflow: Clip war 20s im Fenster [0,20]; Range-Delete kürzt
    // den Clip auf 8s. Früher: Fenster schnappte auf [0,8] = Sprung.
    const auto w = editorview::ensureUsable ({ 0.0, 20.0 }, 8.0);

    CHECK_THAT (w.startLocal, WithinAbs (0.0,  1e-9));
    CHECK_THAT (w.len,        WithinAbs (20.0, 1e-9));   // Anker steht — kein Zusammenschnappen
}

TEST_CASE ("EditorView: window starting before the clip is preserved (right split piece)", "[editorview][dock]")
{
    // Handover auf das rechte Teilstück: lokal negative Startzeit, Fenster
    // schneidet den Clip → bleibt unverändert (Zeitachse steht).
    const auto w = editorview::ensureUsable ({ -3.0, 10.0 }, 5.0);

    CHECK_THAT (w.startLocal, WithinAbs (-3.0, 1e-9));
    CHECK_THAT (w.len,        WithinAbs (10.0, 1e-9));
}

TEST_CASE ("EditorView: window that misses the clip entirely fits the clip", "[editorview][dock]")
{
    // Kein Schnitt mit dem Clip (z.B. Handover auf einen weit entfernten Clip)
    const auto before = editorview::ensureUsable ({ -20.0, 5.0 }, 12.0);   // endet vor 0
    CHECK_THAT (before.startLocal, WithinAbs (0.0,  1e-9));
    CHECK_THAT (before.len,        WithinAbs (12.0, 1e-9));

    const auto after = editorview::ensureUsable ({ 50.0, 5.0 }, 12.0);     // beginnt nach Ende
    CHECK_THAT (after.startLocal, WithinAbs (0.0,  1e-9));
    CHECK_THAT (after.len,        WithinAbs (12.0, 1e-9));
}

TEST_CASE ("EditorView: degenerate inputs stay usable", "[editorview][dock]")
{
    // Mindestlänge wird erzwungen
    const auto tiny = editorview::ensureUsable ({ 1.0, 0.0 }, 10.0);
    CHECK (tiny.len >= editorview::kMinLen);

    // Leerer Clip → definierter Minimal-Zustand, keine NaN/Div-0-Falle
    const auto empty = editorview::ensureUsable ({ 3.0, 5.0 }, 0.0);
    CHECK_THAT (empty.startLocal, WithinAbs (0.0, 1e-9));
    CHECK (empty.len >= editorview::kMinLen);
}

// ---------------------------------------------------------------------------
// editorview::clampZoomLen — Überhang erhalten, Auszoomen begrenzt
// ---------------------------------------------------------------------------

TEST_CASE ("EditorView: zoom-out is bounded by max(clip, current window)", "[editorview][dock]")
{
    // Fenster ohne Überhang: Auszoomen endet an der Clip-Länge (wie bisher)
    CHECK_THAT (editorview::clampZoomLen (30.0, 12.0, 8.0), WithinAbs (12.0, 1e-9));

    // Fenster MIT Überhang (nach Range-Delete): der Überhang bleibt zoombar,
    // wächst aber nicht weiter
    CHECK_THAT (editorview::clampZoomLen (40.0, 8.0, 20.0), WithinAbs (20.0, 1e-9));

    // Einzoomen unter die Mindestlänge wird abgefangen
    CHECK_THAT (editorview::clampZoomLen (0.001, 8.0, 1.0),
                WithinAbs (editorview::kMinLen, 1e-9));

    // Normales Einzoomen bleibt unangetastet
    CHECK_THAT (editorview::clampZoomLen (4.0, 8.0, 8.0), WithinAbs (4.0, 1e-9));
}

// ---------------------------------------------------------------------------
// AutomationLane::laneXToTime / laneTimeToX — der Dock-Automation-Bug
// ---------------------------------------------------------------------------

TEST_CASE ("AutomationLane mapping: arrangement convention (origin 0) unchanged", "[editorview][automation]")
{
    // Arrangement: volle-Breite-Lane, x=0 = t=0 (Bestandsverhalten, darf sich
    // durch das neue Viewport-Mapping NICHT ändern)
    CHECK_THAT (AutomationLane::laneXToTime (200.0f, 100.0, 0.0), WithinAbs (2.0, 1e-6));
    CHECK_THAT (AutomationLane::laneTimeToX (2.0,    100.0, 0.0), WithinAbs (200.0, 1e-4));
}

TEST_CASE ("AutomationLane mapping: viewport origin puts points under the waveform", "[editorview][automation]")
{
    // Der User-Befund vom 2026-07-06: Clip startet bei 30s, View scrollt 4s in
    // den Clip → Lane-x=0 muss t=34s sein. Ohne origin landete der Punkt bei
    // t=x/pps ≈ Songanfang — dort hat die Spur kein Audio → "Automation tut nichts".
    const double pps    = 50.0;
    const double origin = 30.0 + 4.0;

    const double t = AutomationLane::laneXToTime (100.0f, pps, origin);
    CHECK_THAT (t, WithinAbs (36.0, 1e-6));            // 34s + 100px/50pps

    // Roundtrip: Punkt zeichnet an derselben Stelle, an der er gesetzt wurde
    CHECK_THAT (AutomationLane::laneTimeToX (t, pps, origin), WithinAbs (100.0, 1e-4));

    // Regressions-Check des alten Fehlbilds: OHNE origin wäre der Punkt 34s zu früh
    CHECK_THAT (AutomationLane::laneXToTime (100.0f, pps, 0.0), WithinAbs (2.0, 1e-6));
}
