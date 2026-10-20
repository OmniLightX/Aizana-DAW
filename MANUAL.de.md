[English](MANUAL.md) | **Deutsch**

# Aizana – Bedienungsanleitung

Für Aizana 0.3.0 Alpha · Stand: 20. Oktober 2026

Diese Anleitung führt vom ersten Projekt über Aufnahme und Mixing bis zum
Export. Sie beschreibt den aktuellen macOS-Stand. Einzelne Dialoge und
Plugin-Menüs verwenden noch englische Bezeichnungen; diese sind hier dort
angegeben, wo sie beim Auffinden helfen.

Die Alpha wird als Quellcode zum Selbstbauen vorbereitet. Die
[Build-Anleitung](README.de.md#aus-dem-quellcode-bauen) und
[Startanleitung](docs/ALPHA_INSTALL.md) beschreiben die Voraussetzungen.
Der geprüfte Zielrechner ist ein Mac mit Apple Silicon und macOS 13.3 oder neuer.
Diese Anleitung ersetzt keine vollständige Geräte- und Plugin-Abnahme.
Den dokumentierten Prüfstand findest du in [STATUS.md](STATUS.md).

## Inhalt

1. [Begriffe und Oberfläche](#1-begriffe-und-oberfläche)
2. [Die erste Session](#2-die-erste-session)
3. [Audio importieren](#3-audio-importieren)
4. [Aufnehmen](#4-aufnehmen)
5. [Loop-Aufnahme und Comping](#5-loop-aufnahme-und-comping)
6. [Clips bearbeiten](#6-clips-bearbeiten)
7. [Mischen und Signalwege](#7-mischen-und-signalwege)
8. [Plugins und Insert-Ketten](#8-plugins-und-insert-ketten)
9. [MIDI und Instrumente](#9-midi-und-instrumente)
10. [KI-Setup](#10-ki-setup-für-vorhandene-einzelspuren)
11. [KI-Mix und Referenzen](#11-ki-mix-und-referenzen)
12. [Stem-Separation](#12-einen-song-in-stems-trennen)
13. [KI-Studio und Plugin-Generator](#13-ki-studio-und-plugin-generator)
14. [Export](#14-mix-und-spuren-exportieren)
15. [Speichern und Wiederherstellen](#15-speichern-und-wiederherstellen)
16. [Fehlerhilfe](#16-wenn-etwas-nicht-funktioniert)
17. [Tastatur und Maus](#17-tastatur-und-maus)
18. [Lizenzen und weitere Dokumentation](#18-lizenzen-und-weitere-dokumentation)

## 1. Begriffe und Oberfläche

| Begriff | Bedeutung |
|---|---|
| Session | Dein Projekt mit Spuren, Bearbeitungen, Routing und Pluginzuständen |
| Spur | Ein Platz für Audio- oder MIDI-Clips und deren Signalweg |
| Clip | Ein Audio- oder MIDI-Abschnitt im Arrangement |
| Arrangement / Edit | Zeitliche Anordnung der Clips |
| Mixer / Mix | Kanalzüge für Pegel, Panorama, Effekte und Routing |
| Insert | Ein Effekt oder Instrument im Signalweg eines Kanals |
| Bus | Gemeinsamer Ausgang für mehrere Spuren, etwa alle Drums |
| FX-Kanal | Parallel beschickter Effektkanal, beispielsweise für gemeinsamen Hall |
| Send | Anteil eines Signals, der zusätzlich an einen FX-Kanal geschickt wird |
| Master | Gemeinsamer Hauptausgang der Mischung |
| Stem | Eine Einzelspur oder Instrumentgruppe; nicht zwingend ein einzelner Klang |
| Monitoring | Mithören eines Eingangssignals |

Oben liegt die Transportleiste mit Wiedergabe, Stopp, Aufnahme, Tempo, Metronom
und Loop. **Edit** und **Mix** wechseln den Schwerpunkt der Darstellung.
Über **Ansicht → Einsteiger-Welt** wechselst du die Bedienansicht derselben
Session. Die Ansicht erzeugt keine zweite Kopie deines Songs.

Spurauswahl und Clipauswahl sind verschieden: Zum Bearbeiten einer Aufnahme
wählst du den Clip; für Eingang, Solo oder Aufnahmebereitschaft den Spurkopf.
Pluginfenster können eigene Tastatureingaben abfangen. Klicke zurück ins
Arrangement, bevor du einen DAW-Tastaturbefehl verwendest.

## 2. Die erste Session

1. Starte die selbst gebaute App.
2. Öffne **Datei → Neue Session**.
3. Wähle **Datei → Speichern unter…**, gib dem Song einen Namen und lege einen
   eigenen Projektordner an. So hat die Session früh einen bewussten Speicherort.
4. Öffne **Aizana → Einstellungen…** und kontrolliere die Audioeinstellungen.
   Wähle das Gerät, über das du hören und gegebenenfalls aufnehmen möchtest.
5. Importiere zum ersten Test eine kurze Audiodatei.
6. Starte mit der Leertaste. Prüfe, ob sich der Abspielkopf und die Pegelanzeigen
   bewegen und das Signal am gewählten Ausgang zu hören ist.
7. Stoppe und speichere mit **Command-S**.

Unter **Song → Audiogerät einrichten…** findest du die Zuordnung benannter
Eingangsbusse zu Hardwarekanälen. Diese Zuordnung ist vom Mix-Bus zu unterscheiden:
Ein Eingangsbus beschreibt, woher eine Aufnahme kommt, ein Mix-Bus bündelt
bereits vorhandene Spuren.

Für den Einstieg reicht eine kleine Testsession. Bevor du ein wichtiges Projekt
in einer neuen Version öffnest, sichere den vollständigen Sessionordner.

## 3. Audio importieren

### Eine Datei oder mehrere vorhandene Spuren

Ziehe Audiodateien aus dem Finder ins Arrangement oder nutze
**Datei → Audio importieren…**. Mehrere Dateien können auf eigenen Spuren landen.
Prüfe danach Position, Spurname und hörbaren Inhalt.

Wenn zusammengehörige Stems denselben Startpunkt haben, behalte diese gemeinsame
Zeitposition beim Import bei. Eine leise oder stumme Einleitung kann Teil der
gewollten Synchronisierung sein. Schneide sie nicht für jede Datei unabhängig ab.

### Import mit Zuordnungshilfe

**Datei → Stems importieren (KI-Setup)…** analysiert bereits getrennte Spuren
und schlägt Namen, Gruppen und Pegelanpassungen vor. Vor **Anwenden** kannst du
die Zuordnung überprüfen. Der genaue Ablauf steht in Kapitel 10.

## 4. Aufnehmen

### Eingang und Aufnahmebereitschaft

1. Erzeuge eine Spur mit **Command-T**.
2. Wähle am Spurkopf den Audioeingang. Kontrolliere den tatsächlichen
   Hardwarekanal; ein Mikrofon an Eingang 1 liefert kein Signal an Eingang 2.
3. Bei Bedarf verwalte die Eingangsbusse über **Song → Audiogerät einrichten…**
   oder **Manage Buses…** im Eingangsmenü. Gib ihnen verständliche Namen.
4. Wähle das passende Mono-/Stereo-Spurformat. Der Formatschalter allein
   verbindet keinen anderen Hardwareeingang.
5. Aktiviere **REC** am Spurkopf. Das macht die Spur aufnahmebereit.
6. Aktiviere bei Bedarf **I** zum Mithören durch Aizana.
7. Kontrolliere den Eingangspegel und starte erst dann die Aufnahme in der
   Transportleiste.
8. Stoppe, höre die Aufnahme ab und speichere.

**REC am Spurkopf und Aufnahme im Transport sind zwei getrennte Schritte.**
Beim ersten Mikrofonzugriff kann macOS eine Berechtigung verlangen. Wenn keine
Aufnahme ankommt, kontrolliere zusätzlich die Mikrofonfreigabe in den
macOS-Systemeinstellungen.

Beim Mithören über Lautsprecher kann eine Rückkopplung entstehen. Verwende für
Mikrofonaufnahmen Kopfhörer. Wenn dein Interface bereits direktes Monitoring
liefert, vermeide unbeabsichtigtes doppeltes Mithören über Interface und DAW.

### Metronom, Vorzähler und Punch

Das Metronom schaltest du in der Transportleiste ein. Das zugehörige Menü bietet
Vorzähler, Pre-Roll und Klicklautstärke. Stelle Tempo und Taktart passend ein,
bevor du rhythmisch aufnehmen möchtest.

**Auto-Punch** beschränkt die Aufnahme auf den Bereich zwischen den
Loop-In-/Out-Markern. Kontrolliere diese Grenzen vor dem Start; ein falsch
gesetzter Bereich kann eine vermeintlich fehlende Aufnahme erklären.

Physische MIDI-Keyboard- und separate Hardware-Cue-Tests sind für diesen
Release-Stand nicht vollständig abgenommen. Das ist von gewöhnlicher
Audiowiedergabe und dem dokumentierten Aufnahmeweg zu unterscheiden.

## 5. Loop-Aufnahme und Comping

1. Setze die Loop-In-/Out-Marker auf den aufzunehmenden Abschnitt.
2. Aktiviere Loop, wähle den Eingang und schalte die Spur aufnahmebereit.
3. Starte die Aufnahme und spiele den Abschnitt über mehrere Durchläufe ein.
4. Stoppe und öffne die Take-Ansicht des aufgenommenen Clips.
5. Wähle einen ganzen Take zum Gegenhören oder stelle Abschnitte verschiedener
   Takes zu einem Comp zusammen.
6. **Zusammengesetzt (Comp)** führt zur erhaltenen Zusammenstellung zurück,
   wenn du zuvor einen vollständigen Take angehört hast.
7. Höre die Übergänge ab und speichere die Session.

Ein Comp ist eine Auswahl aus den Takes. Bewahre die dazugehörigen Audiodateien
auf. Für den Austausch oder eine Sicherung reicht die Sessiondatei allein nicht.

## 6. Clips bearbeiten

### Auswählen und bewegen

Klicke einen Clip an. **Command-Klick** fügt weitere Clips zur Auswahl hinzu
oder entfernt sie daraus. Ein Auswahlrahmen über einer freien Fläche wählt
Clips im Bereich. Mit dem Pfeilwerkzeug verschiebst du Clips zeitlich oder auf
eine andere Spur. Kontrolliere nach dem Ablegen Startposition und Zielspur.

Das Raster hilft beim rhythmischen Platzieren. **Alt/Option** kann beim
Clipziehen das Einrasten vorübergehend aufheben. Dieselbe Taste hat beim
Pluginziehen eine andere Funktion; siehe Kapitel 8.

### Schneiden, trimmen und Lautstärke

- **Command-E:** am Abspielkopf teilen.
- **C:** Scherenwerkzeug; **Escape:** zurück zum Pfeil.
- Ziehen an der unteren Hälfte des linken oder rechten Cliprands trimmt den
  sichtbaren Audioabschnitt.
- Fade-Griffe an den oberen Ecken verändern Ein- und Ausblendung.
- Der Clip-Gain-Griff oben in der Mitte ändert den Pegel des Clips.
- Zwei überlappende Audioclips auf derselben Spur auswählen und **X** drücken,
  um einen Crossfade anzulegen.
- **Command-Z** nimmt eine Bearbeitung zurück.

Trimmen verschiebt die Abspielgrenzen des Clips; es ist kein Löschen des
Originalaudiomaterials. Nach Schnitten und Fades die Schnittstellen abhören,
besonders bei Gesang, tiefen Tönen und langen Hallfahnen.

### Tempo und weiterführende Bearbeitung

Das Transporttempo und die Taktart bestimmen das musikalische Raster.
**Tempo tippen** ermittelt ein Tempo aus mehreren Klicks im Takt.
Tempoänderungen, Audio-Stretching und Warp-Bearbeitung sind unterschiedliche
Eingriffe: Eine Änderung des Rasters allein garantiert nicht, dass jede
importierte Datei musikalisch passend mitläuft.

Für weiterführende Audiobearbeitung stehen unter anderem Bounce in Place,
Transienten-/Warp-Werkzeuge und Offline-Tonhöhenkorrektur zur Verfügung.
Arbeite dafür zunächst mit einer Kopie der Session und vergleiche das Ergebnis
mit dem Original. Die konkreten verfügbaren Aktionen hängen von Cliptyp und
Auswahl ab.

## 7. Mischen und Signalwege

### Pegel und Panorama

Öffne **Mix** oder drücke **Command-M**. Jeder Kanal bietet Pegel, Panorama,
Mute/Solo und Inserts. Der Master steht rechts. Beginne mit einem hörbaren
Lautstärkeverhältnis, bevor du viele Effekte einsetzt.

**Mute** schaltet eine Spur stumm; **Solo** hilft beim gezielten Abhören.
Beende Solo zum Prüfen der gesamten Mischung. Mehrere ausgewählte Spuren können
gemeinsam geschaltet werden. Prüfe die Auswahl, bevor du **M** oder **S** drückst.

### Kanäle auswählen, verschieben und kopieren

Ein Klick in den Kanalzug wählt die Spur aus. Freie Flächen neben Fader und
Pan verändern keinen Wert. Bediene den Pegel direkt am Fader oder seiner Skala;
den Pan-Knopf ziehst du horizontal. Ein Klick auf einen Regler innerhalb einer
bestehenden Mehrfachauswahl erhält diese Auswahl für die gemeinsame Bedienung.

Ziehe den Kanalnamen oder eine freie Fläche im Kanalzug auf einen anderen
Audiokanal. Die Einfügemarkierung zeigt, ob die Spur davor oder dahinter landet.
Die Reihenfolge im Arrangement folgt mit. Spuren bleiben dabei in ihrem
bestehenden Ordner; Bus-, FX-, VCA- und Cue-Kanäle werden nicht mitverschoben.

Über **Spur duplizieren** im Kontextmenü des Spurkopfs oder Mixerkanals entsteht
direkt neben dem Original eine Kopie mit Clips, Effekten, Einstellungen und
Automation. Audiodateien werden gemeinsam referenziert; Clips, Effekte und
Automationsdaten lassen sich unabhängig bearbeiten. Die Kopie ist nicht
aufnahmebereit und verwendet keinen Freeze-Cache des Originals. Duplizieren
und Verschieben lassen sich rückgängig machen. **Command-D** dupliziert weiterhin
Clips.

Ziehe die Trennkante zwischen Arrangement und Mixer nach oben oder unten, um
dessen Höhe zu ändern. Die Ansichtsschalter bleiben in der unteren Leiste.

### Bus und gemeinsamer Hall

Für eine gemeinsame Bearbeitung mehrerer Spuren:

1. Erzeuge über das Spur-Kontextmenü einen Bus (**Create Bus from Track**).
2. Wähle den gewünschten Bus am Ausgang der zu bündelnden Spuren.
3. Setze gemeinsame Effekte in den Bus und kontrolliere den Weg zum Master.

Für parallelen Hall:

1. Erzeuge einen FX-Kanal (**Create FX Channel**).
2. Füge dort einen Hall ein.
3. Beschicke den FX-Kanal über Sends der gewünschten Spuren.
4. Stelle den Hallanteil mit den Sends und dem FX-Kanalpegel ein.

Ein Bus ersetzt typischerweise den direkten Ausgang einer Spur. Ein Send
ergänzt einen parallelen Weg. Vermeide unbeabsichtigte Doppelroutings.
Sidechain dient dagegen dazu, einen Effekt durch eine andere Spur steuern zu
lassen; diese Steuerung ist nicht dasselbe wie ein hörbarer Send.

### Automation

Öffne die Automationsansicht über den **A**-Schalter oder per Rechtsklick auf
den Spurkopf → **Automation → Einblenden**. Dort kannst du sie auch ausblenden.
Unter **Parameter wählen** stehen Lautstärke, Panorama und die verfügbaren
Effektparameter; die Auswahl öffnet die entsprechende Kurve. Ein-/Ausblenden
und Parameterwahl ändern weder Kurvendaten noch den Schreibmodus.
Punkte bzw. Linien beschreiben den zeitlichen Verlauf des Parameters.
Die Modi **Read**, **Touch**, **Latch** und **Write** stehen sowohl im
Automation-Untermenü als auch in der Automationsspur zur Verfügung:


- **Read:** vorhandene Automation wiedergeben.
- **Touch:** während der Bedienung Automation schreiben.
- **Latch:** nach einer Bedienung den Wert weiter schreiben.
- **Write:** Automation während des Schreibens überschreiben.

Teste schreibende Modi zunächst in einer Kopie. Schalte anschließend auf
**Read** zurück und kontrolliere den Verlauf.

## 8. Plugins und Insert-Ketten

### Plugins finden und öffnen

Aizana enthält eigene Instrumente und Effekte. Fremde VST3-, AU- und
CLAP-Plugins installierst du separat. Führe danach
**Ansicht → Nach Plug-Ins suchen…** aus.

Klicke im Insert-Kopf auf **+**, um ein Plugin hinzuzufügen. Ein Doppelklick
auf den Eintrag öffnet seinen Editor. Ein einfacher Klick wählt den Eintrag aus;
die Auswahl ist farblich erkennbar. Der Punkt rechts am Eintrag schaltet den
Effekt aktiv oder auf Bypass.

### Kopieren und Verschieben

| Geste | Ergebnis |
|---|---|
| Einzelnen Plugin-Eintrag auf einen anderen Kanal ziehen | Plugin mit Einstellungen kopieren; Original bleibt |
| Beim Ablegen auf dem anderen Kanal **Alt/Option (⌥)** halten | Plugin verschieben; Original verlässt den Quellkanal |
| Insert-Titel auf einen anderen Kanal ziehen | Gesamte Effektkette kopieren |
| Beim Ablegen der Kette **Alt/Option** halten | Kette verschieben |
| Plugin innerhalb derselben Liste ziehen | Reihenfolge ändern |

Die Option-Taste befindet sich auf der Mac-Tastatur neben Command und kann mit
**⌥**, **option** oder **alt** beschriftet sein. Entscheidend ist beim
kanalübergreifenden Ziehen der Zustand beim Ablegen.

Greife für ein einzelnes Plugin dessen Eintrag an, nicht den Insert-Titel.
Kopieren ergänzt die Zielkette. **Ersetzen** ist eine eigene Menüaktion.
Über das Pfeilmenü kannst du Ketten kopieren, einfügen, speichern und laden.
Kettenpresets verwenden die Endung `.aizfxchain`.

Kopien übernehmen Einstellungen, Reihenfolge und Bypass, aber keine
Songautomation oder externen Sidechain-Quellverweise. Nach einer Kopie deshalb
Sidechain und Automation separat kontrollieren. Verschieben erhält die Instanz
mit ihren Verknüpfungen.

### Ausgelagerte Pluginfenster

Beim optionalen Hosting in einem separaten Prozess öffnet Aizana direkt den
nativen Plugin-Editor. Ein zusätzliches Hinweisfenster ist dafür nicht nötig.
Schließen und erneutes Öffnen sollen dieselbe Plugininstanz weiterverwenden.
Beim Entfernen des Plugins oder Sessionwechsel werden die zugehörigen Fenster
geschlossen. Native ausgelagerte Editorfenster lassen sich nicht andocken.

Der Ablauf ist mit Fresh Air geprüft. Andere Hersteller und Formate können
abweichendes Verhalten zeigen. Ein separater Prozess ist keine Garantie gegen
jeden Pluginfehler.

## 9. MIDI und Instrumente

Eine MIDI-Spur enthält Noten und Steuerdaten, keine fertige Audioaufnahme.
Damit sie hörbar wird, braucht sie ein Instrument.

1. Füge einer geeigneten Spur ein Instrument hinzu, etwa **Synth (Poly)** oder
   **Drums (Drum Machine)** aus dem Instrumentmenü.
2. Öffne einen MIDI-Clip per Doppelklick in der Piano-Roll.
3. Zeichne Noten ein und passe Position und Länge an.
4. Bearbeite die Anschlagstärke in der Velocity-Spur.
5. Nutze Quantisierung und Swing nach Bedarf und höre die Wirkung ab.

Die Controller-Spur ist zusätzlich einblendbar. Der Synth bietet zwei
Oszillatoren und Presets. Das Drum-Instrument besitzt einen 8×16-Sequencer und
kann eigene WAV-Samples verwenden.

**Song → Tastatur spielen (Musical Typing)** öffnet die Eingabe über die
Computertastatur. Das ersetzt keine Abnahme eines externen MIDI-Keyboards.
Für ein externes Gerät muss dessen MIDI-Eingang an der Spur gewählt sein.

## 10. KI-Setup für vorhandene Einzelspuren

Dieses Werkzeug passt zu einem Ordner mit bereits getrennten Aufnahmen,
beispielsweise Bass, Drums, Gitarren und Gesang.

Die folgende Vorschau erscheint im **Pro-Modus**. In der Einsteiger-Welt wird
der Plan direkt angewendet und kann rückgängig gemacht werden. Wenn du die
Gruppen vorab selbst zuordnen möchtest, schalte die Einsteiger-Welt vorher
über das Menü Ansicht aus.

1. Wähle **Datei → Stems importieren (KI-Setup)…**.
2. Wähle die zusammengehörigen Audiodateien.
3. Warte auf die Analyse.
4. Prüfe in **KI-Setup — erkannte Stems** die Dateinamen, vorgeschlagenen
   Spurnamen, Gruppen und Gain-Werte.
5. Korrigiere falsche Zuordnungen, etwa wenn ein Effekt wegen seines Dateinamens
   als Gesang erkannt wurde.
6. Klicke **Anwenden** und höre die eingerichtete Session ab.

Dateinamen und Audioanalyse liefern Hinweise, keine sichere Instrumenterkennung.
Für KI-Setup brauchst du weder das 355-MB-Stem-Modell noch einen Claude- oder
Codex-Zugang. Es zerlegt keine gemischte Datei.

## 11. KI-Mix und Referenzen

**KI-Mix** in der Transportleiste bzw. **Song → KI-Mix (Heuristik)…** erstellt
einen regelbasierten Mischvorschlag. Dafür ist kein mitgeliefertes trainiertes
Mixing-Modell erforderlich.

1. Speichere deine Ausgangssession.
2. Kontrolliere Spuren, Gruppen und Routing.
3. Wähle optional unter **Song → KI-Referenz** ein Genreprofil oder analysiere
   eine Referenzdatei, die du dafür verwenden darfst.
4. Starte KI-Mix. Im Einsteigermodus wird der Vorschlag unmittelbar angewendet;
   im Pro-Modus gibt es eine Vorschau.
5. Höre das Ergebnis im Zusammenhang ab und vergleiche es mit dem Ausgangsstand.
6. Passe einzelne Entscheidungen an oder verwende Rückgängig.

Eine Referenz liefert Zielwerte und Orientierung. Eine angezeigte LUFS-Zahl ist
kein automatisch verbindliches Lautheitsziel. KI-Mix ersetzt weder die Prüfung
des Exports noch deine klangliche Entscheidung.

## 12. Einen Song in Stems trennen

Hier wird eine **gemischte Audiodatei** in Instrumentgruppen zerlegt.
Das ist ein anderer Vorgang als der Import vorhandener Stems.

1. Wähle **Song → Song in Stems trennen (KI)…**.
2. Wähle die Quelldatei.
3. Wenn das Modell noch fehlt, bietet Aizana einen einmaligen Download von
   ungefähr **355 MB** an. Bestätige ihn nur, wenn du die Funktion nutzen möchtest.
4. Nach Download und Prüfung wählst du:
   - **6 Spuren — mit Gitarre und Piano:** Schlagzeug, Bass, Gesang, Gitarre,
     Klavier und Übriges.
   - **4 Spuren — Gitarre und Piano in Übriges:** Schlagzeug, Bass, Gesang und
     Übriges.
5. Warte auf die lokale Berechnung. Die Fortschrittsanzeige bietet einen Abbruch.
6. Prüfe im Pro-Modus die anschließende Zuordnung im KI-Setup und übernimm das
   Ergebnis. In der Einsteiger-Welt wird es unmittelbar angewendet.
7. Höre jede Ausgabe einzeln und im Zusammenhang ab.

Beide Ausgabeformen verwenden denselben MVSep-Mega-53-Modellstand. Es werden
keine zwei getrennten Modelle benötigt. „Übriges“ entsteht aus dem Restsignal.
Die Schlagzeugausgabe trennt **nicht** zusätzlich Kick, Snare und Hi-Hat in
eigene Dateien.

Ausgaben unter der Lautstärkegrenze werden beim Import übersprungen und
gemeldet. Deshalb müssen nicht immer sechs hörbare Spuren in der Session
erscheinen, etwa wenn eine Instrumentgruppe in der Quelle fehlt.

Die Berechnung erfolgt lokal; für diese Trennung wird dein Audio nicht
hochgeladen. Der Modelldownload benötigt Internetzugriff und wird auf Größe
und Prüfsumme kontrolliert. Python ist zur Nutzung der integrierten Trennung
nicht erforderlich.

**Grenzen:** Übersprechen, fehlende Anteile und Artefakte können auftreten.
Insbesondere synthetische Keyboard-Klangfarben können schlechter getrennt
werden als akustisches Klavier. Die ursprüngliche Mischung aufbewahren.
Falls der Build ohne ONNX Runtime erstellt wurde, steht diese Funktion nicht
zur Verfügung.

**Download-Prüfstand:** Der vollständige Modelltransfer und die Prüfsumme sind
geprüft. Der anonyme Erstdownload über den App-Dialog ist noch nicht abgenommen;
den aktuellen Stand nennt [STATUS.md](STATUS.md).

Herkunft, Lizenz und Prüfgrenzen stehen im
[Modellbericht](docs/OPEN_STEM_RELEASE.md).

## 13. KI-Studio und Plugin-Generator

**Song → KI-Studio (Chat: Plugins bauen lassen)…** öffnet die zusätzliche
Agentenoberfläche. Sie kann lokal installierte Claude-Code- oder Codex-Werkzeuge
verwenden. Deren Installation, Anmeldung, Kosten und Datenübertragung richten
sich nach dem jeweiligen Anbieter. Diese Dienste sind nicht Teil der
kostenlosen Aizana-Lizenz.

**Song → Plugin generieren…** und **Aizana-Plugins verwalten…** gehören zum
integrierten Generator. Zum Erzeugen neuer Plugins werden Faust und die Apple
Command Line Tools auf deinem Rechner benötigt. Diese Werkzeuge werden nicht
mit der App ausgeliefert. Prüfe erzeugte Plugins zunächst in einer Testsession.

Die Einrichtung und technischen Schnittstellen sind in
[docs/MCP.md](docs/MCP.md) beschrieben. Für normales Aufnehmen, Editieren und
Mischen musst du KI-Studio nicht einrichten.

## 14. Mix und Spuren exportieren

1. Speichere die Session und kontrolliere Mute/Solo sowie den Ausgang zum Master.
2. Wähle **Export → Mix exportieren…** für die Gesamtmischung oder
   **Export → Stems exportieren…** für einzelne Spuren.
3. Wähle **WAV**, **AIFF** oder **MP3**, Sample-Rate und Bittiefe bzw. MP3-Bitrate.
4. Wähle eine Zieldatei bzw. einen Zielordner.
5. Starte den Export und warte auf die Abschlussmeldung.
6. Öffne die erzeugte Datei in einem Audioplayer und kontrolliere Anfang, Ende,
   Pegel und Vollständigkeit.

WAV/AIFF unterstützen 44,1 / 48 / 96 kHz und 16 / 24 / 32 Bit.
MP3 unterstützt 44,1 / 48 kHz und konstante 128 / 192 / 256 / 320 kbit/s
(Standard: 320). LAME ist integriert; eine zusätzliche Installation ist nicht nötig.
Beim Wechsel zu MP3 wird eine gewählte Sample-Rate von 96 kHz auf 48 kHz gesetzt.
Die Dateiendung folgt dem Format.

**Abbrechen** beendet den laufenden Export. Auch das Schließen des Exportfensters
bricht ihn ab. Eine fehlgeschlagene oder abgebrochene Datei ersetzt kein vorhandenes
Ziel. Beim Stem-Export bleiben bereits fertiggestellte Dateien im Zielordner;
ein Fehler stoppt die restlichen Stems. Doppelte Spurnamen erhalten nummerierte
Zusätze.

Mixdown berücksichtigt die Master-Effekte. Prüfe deshalb vor dem Rendern,
ob Limiter oder andere Master-Inserts wie gewünscht eingestellt sind.
Stems exportieren rendert vorhandene Spuren; es führt keine KI-Separation
einer fertigen Mischung durch.

## 15. Speichern und Wiederherstellen

**Command-S** speichert die Session. Beim ersten Speichern wählst du Name und
Ort. **Datei → Speichern unter…** dient zum Speichern unter einem anderen Ziel.
Zum Öffnen nutze **Datei → Session öffnen…** und wähle die `.tracktion`-Datei.

Ein Projekt umfasst außerdem seine Medien. Typische Struktur:

```text
My Song/
    My Song.tracktion
    My Song Audio/
        Comps/
        Bounces/
    Backups/
```

Sichere den gesamten Ordner. Bei selbst installierten Plugins können zusätzlich
externe Samplebibliotheken oder Herstellerdateien erforderlich sein; diese
werden durch eine Sessiondatei nicht ersetzt.

Autosave und Backups ergänzen dein bewusstes Speichern. Wenn Aizana eine
Wiederherstellung anbietet, prüfe den Stand vor dem Verwerfen. Bei einer
Speicherfehlermeldung kontrolliere Schreibrechte, freien Speicher und die
Erreichbarkeit externer Laufwerke. Verlasse dich dann nicht darauf, dass die
letzte Änderung bereits gesichert wurde.

## 16. Wenn etwas nicht funktioniert

| Problem | Erste Prüfung |
|---|---|
| Kein Ton | Läuft der Transport? Richtiger Audioausgang? Mute/Solo? Spur zum Bus/Master geroutet? Masterpegel? |
| Kein Eingangssignal | Hardwarekanal, Eingangsbus, Interface und macOS-Mikrofonfreigabe prüfen |
| Keine Aufnahme | Spur-REC und Transportaufnahme getrennt prüfen; Punch-Grenzen kontrollieren |
| Doppeltes oder verzögertes Mithören | Direktmonitoring des Interfaces und DAW-Monitoring vergleichen |
| Plugin fehlt | Unterstütztes Format installiert? Danach erneut nach Plugins suchen |
| Plugin wurde verschoben statt kopiert | Alt/Option beim Ablegen loslassen, wenn eine Kopie gewünscht ist |
| Ganze Insert-Kette wurde gezogen | Einzelnen Plugin-Eintrag statt Insert-Titel greifen |
| Tastenkürzel reagiert nicht | Textfeld oder Plugineditor verlassen und passende DAW-Ansicht fokussieren |
| Falsche KI-Setup-Gruppe | Vorschlag vor Anwenden im Gruppenauswahlfeld korrigieren |
| Stem-Download scheitert | Internet und öffentliche Verfügbarkeit des Modell-Releases prüfen; Kapitel 12 beachten |
| Stem-Ergebnis klingt unsauber | Original behalten; Übersprechen und Modellgrenzen gegenhören |
| MP3-Export schlägt fehl | 44,1 oder 48 kHz wählen; Ziel und freien Speicherplatz prüfen und die Exportfehlermeldung beachten |
| Session meldet fehlende Medien | Vollständigen Projektordner bzw. Sicherung und eingebundene Laufwerke prüfen |
| App oder Plugin stürzt ab | Ausgangssession sichern; Fehler mit kleiner Testsession und konkretem Plugin eingrenzen |

Ein hilfreicher Fehlerbericht enthält Version/Commit, macOS-Version,
Audiogerät, gegebenenfalls Pluginformat und -version sowie reproduzierbare
Schritte. Beschreibe erwartetes und tatsächliches Verhalten.
Prüfe Anhänge auf private Pfade, Zugangsdaten und personenbezogene Angaben.
Veröffentliche nur Audiomaterial, das du teilen darfst.

## 17. Tastatur und Maus

| Befehl | Wirkung |
|---|---|
| Leertaste | Wiedergabe starten/stoppen |
| Command-S | Speichern |
| Command-Z / Command-Shift-Z | Rückgängig / Wiederholen |
| Command-T | Spur hinzufügen |
| Command-M | Mixer umschalten |
| Command-E | Clips am Abspielkopf teilen |
| Command-C / Command-V / Command-D | Clips kopieren / einfügen / duplizieren |
| Command-Klick | Auswahl erweitern oder verkleinern |
| M / S | Mute / Solo gemäß aktiver Auswahl |
| R | Aufnahmebereitschaft der aktiven Spur |
| C / Escape | Schere / Pfeilwerkzeug |
| F | Automatisches Folgen des Abspielkopfs |
| X | Crossfade für zwei überlappende Audioclips derselben Spur |
| Doppelklick auf MIDI-Clip | Piano-Roll öffnen |
| Doppelklick auf Plugin-Eintrag | Plugineditor öffnen |
| Alt/Option beim kanalübergreifenden Insert-Ablegen | Verschieben statt kopieren |

Diese Befehle hängen von Fokus und Auswahl ab. Während einer Texteingabe sollen
Buchstaben den Text bearbeiten, nicht Mute/Solo oder die Aufnahme auslösen.

## 18. Lizenzen und weitere Dokumentation

Die Nutzung von Aizana stellt deine damit erstellte Musik nicht automatisch
unter die Softwarelizenz. Rechte an verwendeten Samples, Aufnahmen und
Drittanbieter-Plugins musst du unabhängig davon beachten.

Aizana ist nicht insgesamt MIT-lizenziert. Der eigene Code verwendet
GPL-3.0-or-later; unter anderem JUCE nutzt hier den AGPLv3-Weg. Das Stem-Modell
hat einen separat dokumentierten MIT-Lizenzweg.

- [Projektübersicht und Build](README.de.md)
- [Lizenzmodell und Weitergabe](docs/OPEN_SOURCE_LICENSING.md)
- [Projektlizenz](LICENSE.md)
- [Fremdkomponenten und Urheberhinweise](THIRD_PARTY_NOTICES.md)
- [Distribution und Quellcodepflichten](docs/DISTRIBUTION.md)
- [Modellherkunft und Trennungsgrenzen](docs/OPEN_STEM_RELEASE.md)
- [Aktueller Prüfstand](STATUS.md)
