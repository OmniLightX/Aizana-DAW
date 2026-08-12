#!/bin/bash
# uitest.sh — Treiber fuer den In-App-Maus-Hook (Ziehbewegungen in GUI-Testlaeufen).
#
# Die Steuerleitung ist eine UNBENANNTE PIPE: dieses Skript startet Aizana als
# Coprozess, schreibt in dessen stdin und liest aus dessen stdout. Diese Pipe hat
# keinen Pfad, keinen Port und keinen Eintrag in irgendeinem Namensraum — nur
# dieser Prozess kann hineinschreiben, und sie stirbt mit ihm. Ein Socket oder
# eine beobachtete Datei waere fuer jeden anderen Prozess adressierbar.
#
# Die App muss dafuer mit -DAIZANA_UITEST_HOOK=ON gebaut sein. Die ausgelieferte
# App kann das NICHT — dort ist der Hook nicht einkompiliert (siehe
# scripts/uitest-guard-check.sh).
#
#   ./scripts/uitest.sh lauf drehbuch.txt      # Drehbuch abfahren
#   ./scripts/uitest.sh lauf -                 # Drehbuch von stdin
#
# Drehbuch-Befehle (eine Zeile pro Schritt):
#   klick <Name>              Knopf/Regler ueber seinen Namen anklicken
#   dklick <Name>             Doppelklick
#   ziehe <Name> <dx> <dy>    Von der Mitte des Elements um dx/dy ziehen
#   ziehevon <A> nach <B>     Von Mitte A zur Mitte B ziehen
#   taste <name> [mods]       return/escape/tab/delete/left/... oder ein Zeichen
#   tippe <text>              Text eintippen
#   menu <top> <itemId>       Menuepunkt der nativen Menueleiste ausloesen
#   drop <Name> <pfad>        Datei auf ein Element fallen lassen (wie aus dem Finder)
#   bild <name>               Screenshot ablegen
#   warte <ms>                Pause
#   namen                     Bedienelemente auflisten
#   > <rohbefehl>             Rohbefehl direkt an den Hook (siehe "hilfe")
#
# Modifier: shift,ctrl,alt,cmd — ctrl+alt+cmd GEMEINSAM wird vom Hook abgelehnt
# (schaltet JUCE-Slider in den Velocity-Modus und wuerde den ECHTEN Mauszeiger
# bewegen; Begruendung in src/testing/UiTestHook.cpp).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${AIZANA_UITEST_APP:-$ROOT/build-uitest/Aizana_artefacts/Release/Aizana.app/Contents/MacOS/Aizana}"
SHOTS="${AIZANA_SHOTS:-/tmp/aizana_gui}"
LOG="${AIZANA_UITEST_LOG:-/tmp/aizana_uitest.log}"
mkdir -p "$SHOTS"

[[ -x "$APP" ]] || { echo "FEHLT: $APP  (cmake -S . -B build-uitest -DAIZANA_UITEST_HOOK=ON && cmake --build build-uitest --target Aizana)"; exit 1; }

# JUCE laesst nur EINE Instanz zu (Main.cpp: moreThanOneInstanceAllowed). Laeuft
# schon eine, wuerde unser Start nur an sie weiterreichen und sich beenden — der
# Hook waere still tot und jeder Befehl liefe ins Leere.
if pgrep -x Aizana >/dev/null 2>&1; then
    echo "ABBRUCH: es laeuft bereits eine Aizana-Instanz. Erst beenden."; exit 1
fi

: > "$LOG"
coproc AIZ { AIZANA_UITEST=1 "$APP" 2>>"$LOG"; }
APP_PID=$!
trap 'kill "$APP_PID" 2>/dev/null; wait "$APP_PID" 2>/dev/null' EXIT

LAST=""
# Antwort lesen: Protokollzeilen sind "| ...", Abschluss ist OK... oder ERR...
# Alles andere ist Log-Rauschen der Engine und wird nur mitgeschrieben.
antwort() {
    LAST=""
    local line
    while IFS= read -r -t "${1:-30}" -u "${AIZ[0]}" line; do
        case "$line" in
            "| "*)   echo "  ${line#| }" ;;
            OK*|ERR*) LAST="$line"; echo "  $line"; [[ "$line" == ERR* ]] && return 1; return 0 ;;
            *) printf '%s\n' "$line" >> "$LOG" ;;
        esac
    done
    LAST="ERR keine Antwort"; echo "  $LAST"; return 1
}

sende() { printf '%s\n' "$1" >&"${AIZ[1]}"; antwort "${2:-30}"; }

# find <Name> -> setzt P/X/Y/L/T/W/H aus der Antwortzeile
P=0; X=0; Y=0; L=0; T=0; W=0; H=0
finde() {
    printf 'find %s\n' "$1" >&"${AIZ[1]}"
    local line
    while IFS= read -r -t 30 -u "${AIZ[0]}" line; do
        case "$line" in
            "OK peer="*)
                eval "$(echo "${line#OK }" | tr ' ' '\n' | sed -e 's/^peer=/P=/' -e 's/^x=/X=/' -e 's/^y=/Y=/' \
                        -e 's/^left=/L=/' -e 's/^top=/T=/' -e 's/^w=/W=/' -e 's/^h=/H=/')"
                return 0 ;;
            ERR*) echo "  $line"; return 1 ;;
            "| "*) echo "  ${line#| }" ;;
            *) printf '%s\n' "$line" >> "$LOG" ;;
        esac
    done
    echo "  ERR keine Antwort auf find"; return 1
}

echo "== Aizana startet (Hook scharf, PID $APP_PID) =="
antwort 20 >/dev/null                       # Begruessung des Hooks

# Auf das Hauptfenster warten
for _ in $(seq 1 40); do
    sende "peers" 10 >/dev/null 2>&1
    [[ "$LAST" == "OK 0" || -z "$LAST" ]] || break
    sleep 1
done
echo "== Fenster da =="

vorn() { osascript -e 'tell application "System Events" to set frontmost of process "Aizana" to true' >/dev/null 2>&1; }

QUELLE="${2:-/dev/stdin}"
[[ "$QUELLE" == "-" ]] && QUELLE=/dev/stdin

fehler=0
while IFS= read -r zeile; do
    zeile="${zeile#"${zeile%%[![:space:]]*}"}"
    [[ -z "$zeile" || "$zeile" == \#* ]] && continue
    befehl="${zeile%% *}"; rest="${zeile#"$befehl"}"; rest="${rest# }"
    echo "-> $zeile"

    case "$befehl" in
        klick|dklick)
            name="${rest% *}"; mods=""
            [[ "$rest" == *" mods="* ]] && { name="${rest%% mods=*}"; mods="${rest##* mods=}"; } || name="$rest"
            finde "$name" || { fehler=1; continue; }
            [[ "$befehl" == klick ]] && k=click || k=dblclick
            sende "$k $P $X $Y ${mods:--}" || fehler=1 ;;
        ziehe)
            name="${rest% * *}"; dx="$(echo "$rest" | awk '{print $(NF-1)}')"; dy="$(echo "$rest" | awk '{print $NF}')"
            finde "$name" || { fehler=1; continue; }
            sende "drag $P $X $Y $(echo "$X + $dx" | bc) $(echo "$Y + $dy" | bc) 24" 60 || fehler=1 ;;
        ziehevon)
            a="${rest%% nach *}"; b="${rest##* nach }"
            finde "$a" || { fehler=1; continue; }; p1=$P; x1=$X; y1=$Y
            finde "$b" || { fehler=1; continue; }
            [[ "$p1" == "$P" ]] || { echo "  ERR verschiedene Fenster"; fehler=1; continue; }
            sende "drag $P $x1 $y1 $X $Y 24" 60 || fehler=1 ;;
        drop)
            name="${rest%% *}"; pfad="${rest#* }"
            finde "$name" || { fehler=1; continue; }
            sende "drop $P ${X%.*} ${Y%.*} $pfad" || fehler=1 ;;
        taste)   sende "key 0 $rest" || fehler=1 ;;
        tippe)   sende "text 0 $rest" || fehler=1 ;;
        menu)    sende "menu $rest" || fehler=1 ;;
        namen)   sende "tree 0" || fehler=1 ;;
        warte)   sende "warte $rest" >/dev/null ;;
        bild)    vorn; sleep 0.4; screencapture -x "$SHOTS/$rest.png"
                 sips -Z 1400 "$SHOTS/$rest.png" --out "$SHOTS/${rest}_klein.png" >/dev/null 2>&1
                 echo "  $SHOTS/${rest}_klein.png" ;;
        vorn)    vorn ;;
        ">")     sende "$rest" 60 || fehler=1 ;;
        *)       sende "$zeile" 60 || fehler=1 ;;
    esac
done < "$QUELLE"

sende "quit" 10 >/dev/null 2>&1
echo "== fertig (Fehler: $fehler) — Log: $LOG =="
exit "$fehler"
