# In-App-Maussteuerung für GUI-Testläufe

**Kurz:** Aizana kann in einem eigens gebauten Testbinary über eine unbenannte Pipe
ferngesteuert werden — Klicken, Ziehen, Tippen, Datei-Drops. Die ausgelieferte App
kann das **nicht**: dort ist der Code nicht einkompiliert.

---

## Warum überhaupt

Die Live-Test-Checkliste enthält Punkte, die nur eine echte Ziehbewegung prüft:
Fades aufziehen, Clip-Gain-Kurven zeichnen, Warp-Marker verschieben, Fader ziehen.
Über die Bedienungshilfen (`scripts/gui-test.sh`) sind Klicks und Namen erreichbar,
Ziehbewegungen nicht.

## Warum nicht systemweit

Der naheliegende Weg wäre ein CGEvent-Hilfsprogramm, das dem System eine Maus
vorspielt. Das trifft aber **alles Sichtbare** — auch ein Benachrichtigungsbanner,
ein Finder-Fenster oder einen „Erlauben?"-Dialog, der im falschen Moment aufgeht.
Die dagegen vorgeschlagenen Absicherungen hielten der Nachprüfung nicht stand:

| Vorgeschlagener Riegel | Befund |
|---|---|
| Koordinaten aufs Fensterrechteck begrenzen | prüft die falsche Frage — ein Rechteck sagt nichts darüber, was dort *obenauf* liegt |
| Screenshot vor/nach | Beweissicherung, kein Riegel |
| nur wenn Aizana im Vordergrund ist | gemessenes Zeitfenster ~100 ms zwischen Prüfung und Wirkung, dazu Fokusklau |
| nicht bei offener User-Session | heuristisch |

Deshalb: **in der App, nicht im System.**

## Wie es funktioniert

Der Hook ruft `juce::ComponentPeer::handleMouseEvent` — genau die Funktion, die
macOS bei einem echten Klick aufruft (`juce_NSViewComponentPeer_mac.mm`,
`sendMouseEvent`). Damit laufen JUCE-Trefferprüfung, echte Handler und echtes
Drücken–Bewegen–Loslassen wie im Alltag.

Die Signatur trägt **kein Ziel**: Punkt, Modifier, Druck, Zeit. Adressiert wird das
Objekt, auf dem die Methode gerufen wird, und `ComponentPeer::getPeer()` zählt nur
Fenster **dieses** Prozesses. Es gibt in der Kette keinen Parameter, über den ein
fremder Prozess benannt werden könnte — das Ereignis betritt den Bildschirm nie.

Die Steuerleitung ist eine **unbenannte Pipe auf stdin**: kein Pfad, kein Port, kein
Eintrag in irgendeinem Namensraum, nur vom startenden Prozess beschreibbar, stirbt
mit ihm. Ein Socket oder eine beobachtete Datei wäre adressierbar — also eine Tür.

## Die Riegel

1. **Übersetzungszeit** — ohne `-DAIZANA_UITEST_HOOK=ON` wird `src/testing/UiTestHook.cpp`
   gar nicht übersetzt; der Header stellt nur ein `constexpr false` bereit. Im
   ausgelieferten Binary existieren die Anweisungen nicht.
2. **Laufzeit** — auch im Testbuild nur bei `AIZANA_UITEST=1` **und** stdin als Pipe.
3. **Werkzeugverbot** — die Übersetzungseinheit referenziert weder CGEvent/CGWarp
   noch Bedienungshilfen, Sockets, Kindprozesse oder Netz (geprüft an der Symboltabelle).
4. **Testmodus verkleinert die App** — bei scharfem Hook bleiben aus: der
   Loopback-Server der KI-Brücke, der CLI-Start des KI-Studios, der Modell-Download.
   Dazu Splash und Mikrofon-Abfrage (native Dialoge, die den Start blockieren).
5. **Modifier-Riegel** — `ctrl+alt+cmd` gemeinsam wird abgelehnt. Sonst kippt
   `Slider::isAbsoluteDragMode` in den Velocity-Modus, der beim Loslassen über
   `MouseInputSource::setRawMousePosition` → `CGWarpMouseCursorPosition` den
   **echten** Mauszeiger bewegt. Zusätzlich bricht jede Geste ab, sobald
   `isUnboundedMouseMovementEnabled()` anspricht.

Belegt durch **fehlgeschlagene Angriffe**, nicht durch Zusagen:

```bash
./scripts/uitest-guard-check.sh
```

Jede Prüfung trägt eine Gegenkontrolle, die beweist, dass sie durchfallen kann.
(Die erste Fassung hatte hier selbst einen Fehler: `set -o pipefail` + `grep -q`
lässt `strings` mit SIGPIPE enden, die Pipeline meldet Misserfolg — der Nachweis am
Auslieferungs-Binary hätte „sauber" gemeldet, auch wenn der Hook drin gewesen wäre.
Gefangen hat es nur die Gegenkontrolle.)

## Fallen, die im ersten Durchlauf zugeschlagen haben

**Gesperrter Bildschirm sieht aus wie ein kaputtes Feature.** Bei aktiver
Bildschirmsperre kommt das Fenster nicht in den Vordergrund und Klicks bleiben
wirkungslos — die Befehle melden aber weiter `OK`, weil sie nur bestätigen, dass
das Ereignis abgeschickt wurde. Ein ganzer Lauf liefert dann Fehlbefunde, die wie
Messwerte aussehen. Genau so entstand am 12.08. das vermeintliche „Quantize greift
nicht"; derselbe Code lief entsperrt einwandfrei, nachgewiesen durch Gegenprobe mit
dem vorherigen Commit-Stand. `uitest.py` bricht deshalb vorher ab (`ioreg` →
`CGSSessionScreenIsLocked`). **Merkmal im Log, falls der Riegel je umgangen wird:**
`vorn` meldet „Fenster meldet keinen Fokus" und `peers` zeigt dauerhaft `focused=0`.

**Modifier kommen NICHT über den Parameter an.** `handleMouseEvent` reicht seinen
`mods` nur an `setButtons` weiter; die Tastatur-Bits eines `MouseEvent` stammen aus
dem globalen `ModifierKeys::currentModifiers`, und `withoutMouseButtons()` wirft die
des Parameters vorher weg (`juce_MouseInputSourceImpl.h:59`). Der Hook setzt den
Static deshalb um jeden Dispatch herum und stellt ihn wieder her. Vorher erreichte
kein Cmd die App — was fälschlich als „Cmd+Klick greift nicht" protokolliert wurde.

**`find` muss gegen JEDEN Vorfahren schneiden, nicht nur gegen das Fenster.** Ein
Clip unter dem unteren Dock liegt im Fensterrechteck und ist trotzdem nicht
anklickbar; JUCE stellt einen Klick nur zu, wenn der Punkt auf jeder Ebene der
Kette enthalten ist. Vorher meldete `find` die Mitte der *vollen* Bounds, der
Doppelklick landete unter dem Dock und meldete `OK`. Jetzt kommt der sichtbare
Streifen zurück — oder ein ehrliches „verdeckt oder weggescrollt".

**Nie auf Stille prüfen.** Der erste GUI-Lauf sah aus wie ein Fehler: der importierte
Clip blieb im Arrangement einfarbig, während der Dock-Editor dieselbe Datei sauber
zeichnete. Nach langer Suche war die Ursache das Testmaterial — die ersten sieben
Sekunden des Stems liegen bei **−90 dB**, also digitaler Stille, und im Fenster war
genau dieser Anfang sichtbar. Der Dock zeigte den ganzen Song und damit auch die
lauten Stellen. Mit einem Ausschnitt ab Sekunde 60 (−17,8 dB) zeichnet das
Arrangement korrekt. **Testmaterial vorher nach Pegel auswählen** (`ffmpeg -af
volumedetect`) — dieselbe Lehre wie bei den Render-Testreihen.

**Zustand herstellen, nicht voraussetzen.** Ansichtsmodus (Pro/Einsteiger) und das
untere Dock sind persistiert, und ein hart beendeter Lauf schreibt sie nicht zurück.
Blind auf einen Umschalter zu klicken öffnet mal und schließt mal. Dafür gibt es
`wennfehlt <Prüfname> @ <Knopf>`.

**Cmd-Kürzel erreichen die App nicht** — sie gehören der nativen Menüleiste. Weg
dafür: `menu <top> <itemId>` (Undo = `menu 2 101`).

**`taste delete` ist NICHT die Taste, die auf einem MacBook „Delete" heißt.** Sie
schickt `KeyPress::deleteKey` (vorwärts löschen); die MacBook-Taste sendet
**Backspace**. Im Arrangement fällt das nicht auf, weil dort beide Codes behandelt
werden — in einem Textfeld schon: steht der Cursor am Ende, tut Vorwärts-Löschen
**nichts**, und der Befehl meldet trotzdem `OK` (das Textfeld hat die Taste ja
angenommen). Genau so sah D4 zuerst nach „greift nicht" aus. Für alles, was die
MacBook-Delete-Taste nachstellen soll: **`taste backspace`**.

## Grenzen (bleiben beim Menschen)

- **PopupMenus reagieren nicht auf synthetische Klicks** — weder Einträge erster
  Ebene noch Untermenüs. `MenuWindow::MouseSourceState::timerCallback` und `isOver()`
  fragen `source.getScreenPosition()` ab, das über `getRawScreenPosition()` auf
  `MouseInputSource::getCurrentRawMousePosition()` geht: die **echte** Systemmaus, die
  der Hook nie bewegt (und nach Riegel 5 auch nicht bewegen darf). Gemessen: ein Klick
  auf „Duplicate" blieb wirkungslos, ein Untermenü ging weder per Klick noch per
  Verweilen auf. **Weg dafür: `press <Name>`** — löst den Eintrag über
  `AccessibilityActionType::press` direkt am Modell aus (bei Untermenüs
  `showSubMenuFor`), ein Methodenaufruf im Prozess wie `menu <top> <itemId>`. Kein
  AXUIElement, keine Freigabe der Bedienungshilfen. Damit sind Kontextmenüs
  vollständig bedienbar; Einträge werden über ihren **Text** adressiert (der steht nur
  im Accessibility-Titel, siehe `describe()`).
- **Native Menüleiste** — auf macOS eine NSMenu (`MainWindow.cpp`, `setMacMainMenu`),
  für Mausereignisse grundsätzlich unerreichbar. Der Hook löst Menüpunkte deshalb
  direkt am `MenuBarModel` aus (`menu <top> <itemId>`) — ein Methodenaufruf im Prozess.
- **Native Dialoge** — `NativeMessageBox` und `FileChooser` (NSOpenPanel) kann der
  Hook *öffnen*, aber nicht bedienen; der Message-Thread blockiert, Kommandos laufen
  in eine Zeitüberschreitung. Rückfallweg: `scripts/gui-test.sh` (Bedienungshilfen,
  spricht Elemente über den **Namen einer benannten App** an, streut keine Ereignisse).
- **Fremd-Plugin-Oberflächen** — eingebettete NSViews bzw. eigener Prozess (OOP).
  Bekommen ihre Ereignisse von AppKit, nicht von JUCE.
- **Die Strecke macOS→JUCE selbst** wird nicht geprüft. Ein Fehler, der genau dort
  säße, fiele nur einem Menschen auf.

## Benutzung

```bash
cmake -S . -B build-uitest -DCMAKE_BUILD_TYPE=Release -DAIZANA_UITEST_HOOK=ON
cmake --build build-uitest --target Aizana -j 10

./scripts/uitest.sh lauf drehbuch.txt      # Drehbuch abfahren
./scripts/uitest.sh lauf -                 # Drehbuch von stdin
```

Für die Editor-Testskripte 01–03 eine eigene, zur Verwendung freigegebene
WAV-Datei angeben. Die Skripte verwenden keine mitgelieferte Musikaufnahme:

```bash
export AIZANA_TESTWAV="/path/to/test.wav"
./scripts/uitest.sh lauf scripts/uitest-drehbuecher/01-editor-vorbereiten.txt
```

Drehbuch-Befehle: `klick` · `dklick` · `ziehe <Name> <dx> <dy>` ·
`ziehevon <A> nach <B>` · `taste` · `tippe` · `menu` · `drop` · `bild` · `warte` ·
`namen`. Rohbefehle des Hooks über `> …` (Hilfe: `> hilfe`).

**Wichtig:** JUCE lässt nur eine Instanz zu — vor einem Lauf darf keine Aizana
laufen, sonst reicht der Start nur weiter und der Hook ist still tot. Das Skript
bricht in dem Fall ab.

**Elemente werden über NAMEN gefunden**, nie über Koordinaten (`setTitle`-Welle,
`f846eb9`). Ist ein Name mehrdeutig, meldet `find` einen Fehler und klickt **nicht**
den ersten Treffer — ein stiller Fehlklick, der Erfolg meldet, ist die Fehlerklasse,
gegen die die ganze Benennung gebaut wurde.

**Gezeichnete Flächen brauchen Namensträger.** Was die App nur *malt* (die Pad-Zeilen
des Drum-Editors, Insert-Slots), hat keinen Namen und ist damit weder für VoiceOver
noch für einen Testlauf greifbar. Weg im Drum-Editor: pro Zeile eine Component, die
**nichts zeichnet und keine Maus annimmt** (`setInterceptsMouseClicks(false,false)`) —
Klick, Rechtsklick und Datei-Drop laufen unverändert an den Editor darunter, aber die
Zeile hat einen Namen. Der Name trägt dort auch den **Zustand** („Pad Snare" vs. „Pad
Snare (Sample)"), sonst wäre eine rein farbliche Anzeige nur per Pixelvergleich
prüfbar.
