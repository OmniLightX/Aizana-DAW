#!/bin/bash
# uitest-guard-check.sh — belegt die Riegel des UI-Test-Hooks durch FEHLGESCHLAGENE
# Angriffe. Hausregel: eine Sicherheitszusage ohne fehlgeschlagenen Angriffsversuch
# ist keine Zusage.
#
#   ./scripts/uitest-guard-check.sh
#
# Prueft:
#   1. Uebersetzungszeit — der ausgelieferte Build enthaelt den Hook NICHT (Artefakt,
#      nicht Quelltext). Der Testbuild enthaelt ihn (sonst prueft der Test nichts).
#   2. Umgebungsvariable — ohne AIZANA_UITEST=1 reagiert der Testbuild nicht.
#   3. Pipe-Pflicht — mit Variable, aber stdin als DATEI: Hook bleibt aus.
#   4. Verbotene Werkzeuge — die Uebersetzungseinheit referenziert weder
#      CGEvent/CGWarp noch Bedienungshilfen, Sockets, Kindprozesse oder Netz.
#   5. Modifier-Riegel — ctrl+alt+cmd gemeinsam wird abgelehnt (wuerde sonst ueber
#      JUCEs Velocity-Modus den ECHTEN Mauszeiger bewegen).
#   6. Testmodus verkleinert — kein Loopback-Server der KI-Bruecke waehrend eines Laufs.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTBIN="$ROOT/build-uitest/Aizana_artefacts/Release/Aizana.app/Contents/MacOS/Aizana"
SHIPBIN="${AIZANA_SHIP_BIN:-$ROOT/build-release/Aizana_artefacts/Release/Aizana.app/Contents/MacOS/Aizana}"
MARKER="Aizana UI-Test-Hook scharf"
fehler=0

ok()   { echo "  ✅ $1"; }
nok()  { echo "  ❌ $1"; fehler=1; }
kopf() { echo; echo "== $1"; }

#------------------------------------------------------------------ 1
kopf "1  Uebersetzungszeit: ist der Hook im ausgelieferten Binary?"

# ACHTUNG, hier steckte schon ein Fehler drin: mit `set -o pipefail` steigt
# `grep -q` beim ersten Treffer sofort aus, `strings` bekommt SIGPIPE und endet
# mit 141 — die Pipeline meldet dann MISSERFOLG, obwohl der Treffer da war. Am
# Auslieferungs-Binary haette das "keine Spur" gemeldet, auch wenn der Hook drin
# gewesen waere: ein Nachweis, der nicht durchfallen kann. Deshalb zaehlen statt
# fruehzeitig abbrechen.
spuren() { strings -a "$1" 2>/dev/null | grep -cF "$MARKER" || true; }

if [[ -x "$SHIPBIN" ]]; then
    if [[ "$(spuren "$SHIPBIN")" -gt 0 ]]; then
        nok "AUSLIEFERUNGS-BINARY ENTHAELT DEN HOOK — nicht ausliefern!"
    else
        ok "Auslieferungs-Binary: keine Spur des Hooks ($SHIPBIN)"
    fi
else
    echo "  ⚠️  kein Auslieferungs-Binary unter $SHIPBIN — uebersprungen"
fi

if [[ -x "$TESTBIN" ]]; then
    # GEGENKONTROLLE: findet dieselbe Messung den Hook dort, wo er drin IST?
    # Ohne diese Zeile waere die Zeile darueber wertlos.
    [[ "$(spuren "$TESTBIN")" -gt 0 ]] \
        && ok "Gegenkontrolle: im Testbuild wird derselbe Nachweis fuendig" \
        || nok "Testbuild enthaelt den Hook NICHT — entweder fehlt der Build mit -DAIZANA_UITEST_HOOK=ON, oder die Messung taugt nichts"
else
    echo "  ⚠️  kein Testbuild unter $TESTBIN — 2/3/5/6 werden uebersprungen"
fi

#------------------------------------------------------------------ 4
kopf "4  Verbotene Werkzeuge in der Uebersetzungseinheit"
OBJ="$(find "$ROOT/build-uitest" -name 'UiTestHook.cpp.o' 2>/dev/null | head -1)"
if [[ -n "$OBJ" ]]; then
    verboten='CGEvent|CGWarpMouseCursorPosition|CGPostEvent|AXUIElement|NSAppleScript|ChildProcess|StreamingSocket|DatagramSocket|NamedPipe|WebInputStream|performExternalDragDrop|_system|_popen|_execv'
    alle="$(nm -u "$OBJ" 2>/dev/null | wc -l | tr -d ' ')"

    # GEGENKONTROLLE: misst nm hier ueberhaupt etwas? Bei leerer Symbolliste (falscher
    # Pfad, fremdes Format) wuerde die Suche nach verbotenen Symbolen zwangslaeufig
    # nichts finden und "sauber" melden — wieder ein Nachweis, der nicht fallen kann.
    if [[ "$alle" -lt 20 ]]; then
        nok "nm liefert nur $alle undefinierte Symbole — die Messung selbst taugt nichts"
    else
        treffer="$(nm -u "$OBJ" 2>/dev/null | grep -E "$verboten" || true)"
        [[ -z "$treffer" ]] && ok "keine Referenz auf systemweite Eingaben, Sockets, Kindprozesse, Netz (von $alle Symbolen)" \
                            || { nok "verbotene Symbole referenziert:"; echo "$treffer" | sed 's/^/       /'; }
    fi
else
    echo "  ⚠️  UiTestHook.cpp.o nicht gefunden — uebersprungen"
fi

[[ -x "$TESTBIN" ]] || { echo; echo "Ergebnis: $([[ $fehler -eq 0 ]] && echo BESTANDEN || echo DURCHGEFALLEN)"; exit "$fehler"; }

pgrep -x Aizana >/dev/null 2>&1 && { echo; echo "ABBRUCH: eine Aizana-Instanz laeuft bereits."; exit 1; }

# Jeder Lauf mit HARTER Zeitgrenze und eigenem Abschuss. Ohne die Variable startet
# die App normal, also mit Splash UND Mikrofon-Abfrage — ein nativer Dialog, der
# ewig auf eine Antwort wartet. Ein Lauf, der auf das Ende der App wartet, haengt
# dann fuer immer (genau hier ist die erste Fassung dieses Skripts stehengeblieben).
AUSGABE=""; FEHLERKANAL=""
laufe() {   # $1 = Sekunden, $2 = 1|0 (AIZANA_UITEST setzen), $3 = Kommandos, $4 = stdin-Art (pipe|datei)
    local sek="$1" mitVar="$2" cmds="$3" art="${4:-pipe}"
    local out err eingabe pid i=0
    out="$(mktemp)"; err="$(mktemp)"

    if [[ "$art" == datei ]]; then
        eingabe="$(mktemp)"; printf '%s' "$cmds" > "$eingabe"
        if [[ "$mitVar" == 1 ]]; then AIZANA_UITEST=1 "$TESTBIN" < "$eingabe" >"$out" 2>"$err" &
        else                                        "$TESTBIN" < "$eingabe" >"$out" 2>"$err" & fi
    else
        if [[ "$mitVar" == 1 ]]; then printf '%s' "$cmds" | AIZANA_UITEST=1 "$TESTBIN" >"$out" 2>"$err" &
        else                          printf '%s' "$cmds" |               "$TESTBIN" >"$out" 2>"$err" & fi
    fi

    pid=$!
    while kill -0 "$pid" 2>/dev/null && (( i < sek )); do sleep 1; ((i++)); done
    pkill -x Aizana >/dev/null 2>&1; wait "$pid" 2>/dev/null
    AUSGABE="$(cat "$out")"; FEHLERKANAL="$(cat "$err")"
    rm -f "$out" "$err" ${eingabe:+"$eingabe"}; sleep 1
}

#------------------------------------------------------------------ 2
kopf "2  Angriff: Hook ohne AIZANA_UITEST=1 ansprechen (stdin IST eine Pipe)"
DISCOVERY="$HOME/Library/Aizana/aistudio/live.json"
[[ -f "$DISCOVERY" ]] && vor_normal="$(stat -f %m "$DISCOVERY")" || vor_normal="-"

laufe 12 0 $'peers\nquit\n' pipe

grep -qF "$MARKER" <<< "$AUSGABE" \
    && nok "Hook hat OHNE Umgebungsvariable geantwortet" \
    || ok "keine Reaktion — Riegel haelt"

# GEGENKONTROLLE A: lief die App ueberhaupt? Eine App, die gar nicht startet,
# antwortet auch nicht — das waere ein bestandener Test aus dem falschen Grund.
[[ -n "$FEHLERKANAL" ]] \
    && ok "Gegenkontrolle: die App lief wirklich (Engine-Ausgaben vorhanden)" \
    || nok "kein Lebenszeichen der App — der Test oben beweist nichts"

# GEGENKONTROLLE B fuer Punkt 6: im NORMALEN Lauf muss live.json geschrieben
# werden. Nur wenn die Messung das hier SIEHT, sagt ihr Schweigen im Testlauf etwas.
[[ -f "$DISCOVERY" ]] && nach_normal="$(stat -f %m "$DISCOVERY")" || nach_normal="-"
[[ "$vor_normal" != "$nach_normal" ]] \
    && ok "Gegenkontrolle: im Normalbetrieb schreibt der Server live.json — die Messung greift" \
    || nok "live.json blieb auch im NORMALEN Lauf unveraendert — Punkt 6 unten waere wertlos"

#------------------------------------------------------------------ 3
kopf "3  Angriff: Variable gesetzt, aber stdin ist eine DATEI (Pfad = adressierbar)"
laufe 8 1 $'peers\nquit\n' datei
err="$FEHLERKANAL"
grep -q "stdin ist keine Pipe" <<< "$err" \
    && ok "abgelehnt: nur eine unbenannte Pipe wird akzeptiert" \
    || nok "keine Ablehnung gemeldet (stderr: $(echo "$err" | tail -2 | tr '\n' ' '))"

#------------------------------------------------------------------ 5 + 6
kopf "5+6 Angriff im scharfen Lauf: verbotene Modifier / Loopback-Server"
[[ -f "$DISCOVERY" ]] && vorher="$(stat -f %m "$DISCOVERY")" || vorher="-"

laufe 25 1 $'warte 9000\ndrag 0 100 100 200 200 8 ctrl,alt,cmd\npeers\nquit\n' pipe
ausgabe="$AUSGABE"

grep -q "ERR verboten: ctrl+alt+cmd" <<< "$ausgabe" \
    && ok "ctrl+alt+cmd abgelehnt (kein Warp des echten Mauszeigers)" \
    || nok "Modifier-Riegel hat NICHT gegriffen"

grep -qE "^OK [1-9]" <<< "$ausgabe" \
    && ok "Fenster war da — der Lauf war echt, nicht bloss ein Fehlstart" \
    || nok "kein Fenster im scharfen Lauf (peers leer)"

[[ -f "$DISCOVERY" ]] && nachher="$(stat -f %m "$DISCOVERY")" || nachher="-"
[[ "$vorher" == "$nachher" ]] \
    && ok "Loopback-Server der KI-Bruecke blieb aus (live.json unveraendert)" \
    || nok "live.json wurde im Testlauf geschrieben — Server lief doch"

echo
echo "Ergebnis: $([[ $fehler -eq 0 ]] && echo BESTANDEN || echo DURCHGEFALLEN)"
exit "$fehler"
