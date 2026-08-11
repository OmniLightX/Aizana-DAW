#!/bin/bash
# gui-test.sh — Hilfsmittel fuer GUI-Testlaeufe ueber die Bedienungshilfen.
#
# Die Oberflaeche wird ueber NAMEN angesprochen, nicht ueber Bildschirm-
# koordinaten: ein Klick auf "Stumm Vocals" trifft genau diesen Knopf und
# ueberlebt jedes Verschieben des Fensters. Koordinaten wuerden bei einer
# Layout-Aenderung still ins Leere klicken und trotzdem Erfolg melden.
#
#   ./scripts/gui-test.sh start [session.tracktion]
#   ./scripts/gui-test.sh klick "Wiedergabe"
#   ./scripts/gui-test.sh namen                 # alles Ansprechbare auflisten
#   ./scripts/gui-test.sh wert  "0:00"          # Anzeige zurueckgelesen (Teilstring)
#   ./scripts/gui-test.sh bild  schritt3        # Screenshot ablegen
#   ./scripts/gui-test.sh taste "m" cmd
#   ./scripts/gui-test.sh esc                   # Dialog schliessen
#   ./scripts/gui-test.sh ende
set -uo pipefail
APP="${AIZANA_APP:-$(cd "$(dirname "$0")/.." && pwd)/build/Aizana_artefacts/Release/Aizana.app}"
SHOTS="${AIZANA_SHOTS:-/tmp/aizana_gui}"
mkdir -p "$SHOTS"

se() { osascript -e "tell application \"System Events\" to tell process \"Aizana\" $1" 2>&1; }
vorn() { se 'to set frontmost to true' >/dev/null; sleep 0.4; }

case "${1:-}" in
  start)
    open "$APP" ${2:+"$2"}; sleep 12
    # Erststart fragt ggf. nach der Mikrofon-Berechtigung und blockiert bis zur Antwort.
    osascript -e 'tell application "System Events" to keystroke return' >/dev/null 2>&1
    sleep 6; vorn; echo "gestartet" ;;
  namen)
    vorn; se 'to tell window 1 to return name of every UI element' \
      | tr ',' '\n' | grep -v "missing value" | sed 's/^ //' ;;
  fenster) se 'to return name of every window' ;;
  klick)
    vorn; se "to tell window 1 to click (first UI element whose name is \"$2\")" ;;
  wert)
    vorn; se 'to tell window 1 to return name of every UI element' \
      | tr ',' '\n' | grep -F "$2" | head -1 | sed 's/^ //' ;;
  taste)
    vorn
    case "${3:-}" in
      cmd) se "to keystroke \"$2\" using command down" ;;
      *)   se "to keystroke \"$2\"" ;;
    esac ;;
  esc)  vorn; osascript -e 'tell application "System Events" to key code 53' ;;
  bild)
    vorn; screencapture -x "$SHOTS/${2:-shot}.png"
    sips -Z 1200 "$SHOTS/${2:-shot}.png" --out "$SHOTS/${2:-shot}_small.png" >/dev/null 2>&1
    echo "$SHOTS/${2:-shot}_small.png" ;;
  ende) osascript -e 'tell application "Aizana" to quit' >/dev/null 2>&1; sleep 2
        osascript -e 'tell application "System Events" to key code 53' >/dev/null 2>&1; echo "beendet" ;;
  *) sed -n '2,22p' "$0" ;;
esac
