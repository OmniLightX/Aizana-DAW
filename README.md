<p align="center"><img src="Resources/brand/aizana-banner.png" alt="Aizana — Open-source Digital Audio Workstation" width="820"></p>

# Aizana

Aizana ist eine quelloffene, auf Mixing ausgerichtete DAW für macOS. Aufnahme,
Audio- und MIDI-Bearbeitung, Comping, Busse, FX-Sends und Export gehören zum
Programm. Die Oberfläche ist deutsch. KI-Funktionen sind zusätzliche Werkzeuge;
Aufnahme und Mixing benötigen keinen KI-Account.

[Bedienungsanleitung](MANUAL.md) ·
[Erster Start](docs/ALPHA_INSTALL.md) ·
[Funktionsübersicht](#funktionsübersicht) ·
[Selbst bauen](#aus-dem-quellcode-bauen) ·
[Lizenzen](#lizenz-und-mitwirkung)

## Wie Aizana entstanden ist

Ich bin Audioingenieur und komme aus der Musik, nicht aus der Softwareentwicklung.
Aizana begann mit einer persönlichen Frage: Wie weit komme ich, wenn ich meine
Erfahrung mit Audio und meine Vorstellungen von einer DAW mit den Möglichkeiten
von KI verbinde?

Aus dieser Neugier sind Monate intensiver Arbeit geworden. Ich habe das Projekt
allein mit Unterstützung von KI entwickelt, Ideen ausprobiert, Ergebnisse
getestet, Fehler gesucht und vieles wieder überarbeitet. Die KI hat mir bei der
technischen Umsetzung geholfen. Was daraus werden sollte und wie es sich beim
Musikmachen bedienen lassen soll, habe ich aus meiner eigenen Praxis eingebracht.

Dabei baut Aizana auf bestehenden Open-Source-Projekten auf. Ohne die Arbeit
dieser Entwickler wäre das in dieser Form nicht möglich gewesen. Die verwendeten
Komponenten, ihre Urheber und die jeweiligen Lizenzen sind im Projekt dokumentiert.

Ich teile Aizana und den Quellcode kostenlos mit euch, weil ich zeigen möchte,
was aus einem solchen persönlichen Experiment entstehen kann. Wer ebenfalls Musik
macht, neugierig auf die Technik ist oder selbst daran weiterarbeiten möchte, ist
eingeladen, es auszuprobieren und sich unter den jeweiligen Lizenzbedingungen
einzubringen.

Aizana ist mein Hobbyprojekt, und ich freue mich, das Ergebnis dieser Arbeit mit
euch zu teilen.

## Projektstand und Orientierung

**Kostenlos und Open Source:** Wir stellen den Quellcode von Aizana kostenlos
zum Selbstbauen, Nutzen und Weiterentwickeln bereit. Der eigene Code steht unter
GPL-3.0-or-later; Abhängigkeiten behalten ihre Lizenzbedingungen, insbesondere
JUCE unter AGPLv3. Die enthaltenen Lizenztexte und Fremdhinweise gelten weiterhin.
[Lizenzmodell und Pflichten](docs/OPEN_SOURCE_LICENSING.md).

**Stand: 0.3.0 Alpha, in abschließender Prüfung.** Die aktuelle Quelle enthält die
Recording-, Speicher-, Comping-, MIDI-Editor- und Mixer-Korrekturen sowie die
neue Insert-Kettenverwaltung. Offene Abnahmen stehen in [STATUS.md](STATUS.md).
Eine grüne automatisierte Suite ist keine vollständige Hardware- oder
Plugin-Kompatibilitätszusage. Sichere wichtige Projekte vor Versionswechseln.

## Bezug über GitHub

Aizana 0.3.0 wird ausschließlich als Quellcode über dieses GitHub-Repository
bereitgestellt. Für diesen Stand gibt es kein DMG und keinen fertigen
App-Download. Die Anleitung unter [Aus dem Quellcode bauen](#aus-dem-quellcode-bauen)
beschreibt den lokalen Build einschließlich der Abhängigkeiten.

Der geprüfte Build richtet sich an **Apple Silicon (arm64), macOS 13.3 oder neuer**.
Ein lokaler Build ist nicht Developer-ID-signiert oder notarisiert. Hinweise zum
Start stehen in [ALPHA_INSTALL.md](docs/ALPHA_INSTALL.md). Aizana verändert keine
Gatekeeper-Einstellungen. Frühere Release-Pakete enthalten nicht den aktuellen
Stand von `main`.

## Wofür Aizana gedacht ist

Aizana richtet sich an Menschen, die eigene Musik aufnehmen, vorhandene Spuren
bearbeiten und Songs mischen möchten. Anfänger und erfahrene Anwender arbeiten
mit derselben Session; die Beginner- und Pro-Ansichten unterscheiden sich in der
Darstellung und im Umfang der angebotenen Entscheidungen. Automatische Vorschläge
bleiben normale, überprüfbare DAW-Bearbeitungen mit Rückgängig-Funktion.

Du kannst mit einer leeren Session beginnen und aufnehmen oder vorhandene
Einzelspuren importieren. Im Arrangement legst du den zeitlichen Ablauf fest;
im Mixer bearbeitest du Klang, Pegel und Signalwege. Die fertige Mischung lässt
sich als Audiodatei exportieren. Für diesen Ablauf sind weder ein KI-Abonnement
noch zusätzliche Modellgewichte nötig.

Die Assistenzfunktionen ergänzen diesen Arbeitsablauf: KI-Setup hilft beim
Einordnen vorhandener Einzelspuren, KI-Mix erstellt einen regelbasierten
Mischvorschlag, und die optionale Stem-Separation zerlegt einen fertigen Mix.
Diese drei Aufgaben sind voneinander unabhängig.

### Welche Anleitung brauche ich?

| Dein Ziel | Einstieg |
|---|---|
| Aizana erstmalig bauen und starten | [Build-Anleitung](#aus-dem-quellcode-bauen) und [Erster Start](docs/ALPHA_INSTALL.md) |
| Einen Song importieren, bearbeiten und mischen | [Bedienungsanleitung](MANUAL.md), Kapitel 2–8 |
| Mikrofon oder Instrument aufnehmen | [Aufnahme](MANUAL.md#4-aufnehmen) |
| Vorhandene Stems automatisch zuordnen | [KI-Setup](MANUAL.md#10-ki-setup-für-vorhandene-einzelspuren) |
| Eine gemischte Datei in Instrumentgruppen zerlegen | [Stem-Separation](MANUAL.md#12-einen-song-in-stems-trennen) |
| Fertige Audiodateien ausgeben | [Export](MANUAL.md#14-mix-und-spuren-exportieren) |
| Einen Fehler eingrenzen | [Fehlerhilfe](MANUAL.md#16-wenn-etwas-nicht-funktioniert) |
| Code oder Modell weitergeben | [Lizenzübersicht](docs/OPEN_SOURCE_LICENSING.md) und [Distribution](docs/DISTRIBUTION.md) |

## Funktionsübersicht

| Bereich | Enthaltene Funktionen |
|---|---|
| Aufnahme | Mehrspur-Audio, Eingangswahl, Monitoring, Vorzähler, Metronom, Punch-In/Out, Loop-Takes und Comping |
| Arrangement | Clip-Auswahl und Mehrfachauswahl, Verschieben, Schneiden, Trimmen, Fades/Crossfades, Clip-Gain, Gain-Hüllkurven, Edit-Gruppen und Zusammenfügen |
| Audio-Bearbeitung | Bounce in Place, Time-Stretch, Transientenerkennung, Warp-Marker und tonartbezogene Offline-Tonhöhenkorrektur |
| Tempo | Tempo- und Taktartwechsel, Raster, Quantisierung, Tempo-Ermittlung aus Audio |
| MIDI | Piano-Roll, Notenlänge, Velocity, Controller-Spur, Quantisierungsstärke, Swing und Skalenbezug |
| Mixer | Audio-, Bus-, FX- und Masterkanäle, Sends, Bus-Routing, VCA-Gruppen, Sidechain, Mute/Solo, Panorama und Pegel |
| Anzeigen | Peak/RMS/LUFS, EQ-Spektrum, Wellenformen und Abspielkopf |
| Inserts | Einzelplugin- und Kettenkopien, Verschieben, Reihenfolge, Bypass, Bulk-Entfernen, Presets und gezielt ladbares Default-Preset |
| Automation | Pluginparameter und Mischparameter mit Read/Touch/Latch/Write |
| Instrumente | Aizana Synth mit zwei Oszillatoren und Presets; Aizana Drums mit 8×16-Sequencer und eigenen WAV-Samples |
| Effekte | EQ mit Banddynamik, Kompressor, Reverb, Limiter, De-Esser, Delay, Gate und Saturator |
| Plugin-Hosting | VST3, Audio Units auf macOS und CLAP; externe Plugins separat installieren |
| Export | Stereo-Mixdown und Einzelspuren als WAV/AIFF, 44,1/48/96 kHz und 16/24/32 Bit; MP3-Export ist derzeit nicht implementiert |
| Sessionverwaltung | Speichern, Medienordner, Autosave, Sicherungen und Wiederherstellung |
| Assistenz | KI-Setup für vorhandene Stems, regelbasierter KI-Mix, Referenzanalyse, optionale lokale Stem-Separation und KI-Studio |

Separate Hardware-Cue-Ausgänge und das experimentelle Hosting in getrennten
Prozessen haben eigene Prüfgrenzen. Die Tabelle beschreibt implementierte
Funktionen, keine universelle Geräte- oder Plugin-Kompatibilitätsgarantie.

## In wenigen Schritten zum Projekt

1. Eine neue Session anlegen und mit **Datei → Speichern unter…** Name und Ort
   wählen. Bereits vorhandene Sessions über **Datei → Session öffnen…** laden.
2. Audiodateien in das Arrangement ziehen oder über **Datei → Audio importieren…**
   hinzufügen. Mehrere Dateien können eigene Spuren erhalten.
3. Für Aufnahme den Eingang am Spurkopf auswählen, **REC** scharfschalten und
   bei Bedarf Monitoring **I** aktivieren. Erst dann die Transportaufnahme starten.
   Die Mikrofonfreigabe wird von macOS verwaltet.
4. Mit **Mix** die Konsole öffnen, Pegel und Panorama einstellen. Busse bündeln
   Ausgänge; FX-Kanäle werden über Sends beschickt.
5. Über **Export** Mixdown oder Stems ausgeben. Bereich, Dateiformat,
   Sample-Rate, Bittiefe, Mute/Solo und Master-Effekte vor dem Export prüfen.

## Bearbeiten und Comping

- Clips auswählen, verschieben, an den Rändern trimmen und über die Fade-Griffe
  ein- oder ausblenden. Die Werkzeuge sind oben im Arrangement erreichbar.
- Command-Klick erweitert oder verkleinert die Auswahl. Die Auswahl von
  Spurköpfen und die Auswahl von Clips sind getrennte Vorgänge.
- Command-Z nimmt Bearbeitungen zurück, Command-Shift-Z wiederholt sie.
- Bei Loop-Aufnahme entstehen Takes. In der Take-Ansicht lassen sich Abschnitte
  verschiedener Takes zu einem Comp zusammenstellen. Ein ganzer Take kann zum
  Vergleich gewählt werden; **Zusammengesetzt (Comp)** kehrt zur erhaltenen
  Zusammenstellung zurück.
- Ein Doppelklick auf einen MIDI-Clip öffnet den Editor. Die Piano-Roll zeigt
  Noten neben der Klaviatur; darunter liegt die kompakte Velocity-Spur.
  Die Controller-Spur ist bei Bedarf einblendbar. Noten lassen sich zeichnen,
  verschieben, verlängern, löschen und quantisieren.

## Inserts und FX-Ketten

Audio-, Bus-, FX- und Masterkanäle verwenden denselben Insert-Bereich:

| Bedienung | Wirkung |
|---|---|
| Plus im Insert-Kopf | Pluginauswahl öffnen |
| Doppelklick auf ein Plugin | Pluginfenster öffnen |
| Punkt rechts am Plugin | Dieses Insert aktivieren/deaktivieren |
| Plugin innerhalb der Liste ziehen | Reihenfolge ändern |
| Plugin auf einen anderen Kanal ziehen | Mit Einstellungen kopieren |
| Insert-Titel auf einen anderen Kanal ziehen | Gesamte Effektkette kopieren |
| Alt/Option (⌥) beim kanalübergreifenden Ablegen | Bestehende Instanz bzw. Kette verschieben |
| Power im Kopf | Alle Effekt-Inserts gemeinsam aktivieren/deaktivieren |
| Pfeilmenü | Kette kopieren, einfügen, speichern, laden oder ausdrücklich ersetzen |
| Alle entfernen | Effektkette in einem rückgängig machbaren Schritt entfernen |

Kopieren **ergänzt** die Zielkette; Ersetzen ist ein eigener Menüpunkt. Interne
Routing-, Lautstärke- und Meter-Bausteine sowie Instrumente bleiben geschützt.
Das **Default-Preset** wird gespeichert und auf Wunsch geladen, nicht automatisch
auf neue Kanäle angewendet. Presets haben die Endung `.aizfxchain`.

Wiederverwendbare Kopien übernehmen Parameter, Reihenfolge und Bypass. Sie
kopieren keine zeitbezogene Songautomation oder externe Sidechain-Quellverweise.
Echte Verschiebeaktionen erhalten die ursprünglichen Instanzen und deren
Verknüpfungen. Die neue Bedienung ist implementiert und automatisch getestet;
das kanalübergreifende Alt-/Option-Ziehen ist in der nativen Bedienprüfung bestätigt.
Weitere Prüfgrenzen stehen in [STATUS.md](STATUS.md).

Bei experimentell ausgelagerten Plugins öffnet sich die eigentliche
Pluginoberfläche ohne zusätzliches Hinweisfenster. Dieser Ablauf ist mit
Fresh Air einschließlich Bedienung und Sessionwechsel geprüft; er ist keine
Zusage für sämtliche Drittanbieter-Plugins. Native Fenster ausgelagerter Plugins
lassen sich nicht in das angedockte Editorpanel einbetten.

## Häufige Tastenkürzel

| Taste | Aktion |
|---|---|
| Leertaste | Wiedergabe starten/stoppen |
| Command-S | Session speichern |
| Command-Z / Command-Shift-Z | Rückgängig / Wiederholen |
| Command-T | Spur hinzufügen |
| Command-M | Mixer umschalten |
| Command-E | Am Abspielkopf teilen |
| Command-C / V / D | Clips kopieren / einfügen / duplizieren |
| M / S | Ausgewählte Spur(en) stummschalten / solo |
| R | Aufnahmebereitschaft der aktiven Spur |
| C / Escape | Schere / Pfeilwerkzeug |
| F | Abspielkopf automatisch folgen |

Texteingaben haben Vorrang vor Spur- und Transportbefehlen. Manche Befehle
wirken abhängig von der aktiven Ansicht und Auswahl.

## Dateien und Sicherungen

```text
My Song/
    My Song.tracktion
    My Song Audio/
        Comps/
        Bounces/
    Backups/
```

Beim ersten Speichern werden benötigte Aufnahmen einschließlich alternativer
Takes in den Session-Medienordner übernommen. Vor dem ersten Speichern legt
Aizana Aufnahmen im benutzerspezifischen Musikordner ab. Fehlende Medien werden
als Fehler gemeldet. Autosave und Wiederherstellung ergänzen das ausdrückliche
Speichern; ein Wiederherstellungsangebot soll nicht ungelesen verworfen werden.

## KI-Setup, KI-Mix und Stem-Separation

Enthalten sind ein polyphoner Synthesizer, ein Drum-Instrument mit Sequencer und
Sampler sowie EQ mit Banddynamik, Kompressor, Reverb, Limiter, De-Esser, Delay,
Gate und Saturator. Externe Plugins werden über VST3, Audio Units und CLAP
gehostet und separat installiert; Drittanbieter-Plugins sind nicht mitgeliefert.

**KI-Setup** arbeitet mit bereits vorhandenen Einzelspuren: Dateinamen und
Audioanalyse helfen bei Instrumentzuordnung, Gruppierung, Routing und Gain-Staging.
Es zerlegt keine gemischte Audiodatei und benötigt dafür keine Stem-Modellgewichte.

**KI-Mix** nutzt derzeit Regeln, kein mitgeliefertes trainiertes Mixing-Modell.
Es schlägt Pegel-, EQ-, Dynamik- und Routing-Anpassungen vor. Eine Referenzdatei
oder ein Genreprofil kann zusätzliche Zielwerte liefern. Die Ergebnisse sind
überprüfbar und rückgängig zu machen; automatische Entscheidungen ersetzen nicht
das Gegenhören.

**Stem-Separation** ist ein davon getrenntes Werkzeug zum Aufteilen einer fertigen
Audiodatei.
Stem-Separation läuft lokal mit dem MIT-freigegebenen MVSep-Mega-Modell von
Roman Solovyev (ZFTurbo). Der einmalige optionale Modelldownload umfasst rund
355 MB und wird vor der Verwendung per Größe und SHA-256 geprüft. Sechs Ausgaben
sind möglich: Schlagzeug, Bass, Gesang, Gitarre, Klavier und Übriges als Restsignal;
alternativ vier Kategorien. Python ist dafür nicht erforderlich. Modellgewichte
sind nicht Bestandteil des Quellcode-Repos. Trennfehler sind möglich, besonders
bei synthetischen Keyboard-Klangfarben. Herkunft, volle MIT-Hinweise und Grenzen:
[Stem-Modell und Prüfstand](docs/OPEN_STEM_RELEASE.md). KI-Studio kann
lokal installierte Claude-Code- oder Codex-CLI-Werkzeuge nutzen; deren Konten,
Netzwerkzugriffe und Datenschutzbedingungen gelten separat. Eine pauschale
Zusage, dass bei Nutzung solcher Agenten keine Daten den Rechner verlassen,
wird nicht gemacht.

Der Faust-Plugin-Generator ist integriert. Zur Erzeugung neuer Plugins werden
Faust und die Apple Command Line Tools auf dem jeweiligen Rechner benötigt;
der Compiler wird nicht mit der App ausgeliefert. Details: [docs/MCP.md](docs/MCP.md).

## Aus dem Quellcode bauen

Voraussetzungen: Apple Silicon, macOS 13.3+, Apple Command Line Tools (Clang),
CMake 3.25+, Git und Python 3 für die Prüfskripte; Faust für den Plugin-Generator.
Die Build-Konfiguration verwendet C++20. Abhängigkeiten werden beim Konfigurieren
heruntergeladen. Zum Installieren der Apple Command Line Tools dient
`xcode-select --install`; CMake und Faust müssen im Terminal über ihren Namen
aufrufbar sein. Der erste Build benötigt Internetzugriff und deutlich mehr Zeit
als spätere inkrementelle Builds.

```sh
git clone https://github.com/OmniLightX/Aizana-DAW.git
cd Aizana-DAW
git submodule update --init extern/JUCE extern/tracktion_engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAIZANA_UITEST_HOOK=OFF
cmake --build build --target Aizana engine_tests oop_host_smoke -j4
python3 tests/test_recording_patch.py
python3 tests/test_state_patch.py
ctest --test-dir build --output-on-failure
```

App: `build/Aizana_artefacts/Release/Aizana.app`. Generator und ONNX sind
standardmäßig aktiviert, sofern die notwendigen Werkzeuge vorhanden sind.
`AIZANA_BUILD_GENERATOR=OFF` beziehungsweise `AIZANA_ENABLE_ONNX=OFF` erzeugen
reduzierte Builds. Sie sind nicht funktionsgleich mit dem vollständigen Release.
Der UI-Test-Hook darf in verteilten Builds niemals aktiviert sein.
Die versionierten Tracktion-Patches werden beim Konfigurieren geprüft und
angewendet. Ein abweichender lokaler Dependency-Stand wird nicht überschrieben.

Die App anschließend über Finder öffnen oder:

```sh
open build/Aizana_artefacts/Release/Aizana.app
```

Für Diagnoseprüfungen einen separaten Debug-Build anlegen und ebenfalls über
CTest testen. Dabei muss die normale Aizana-App geschlossen sein, da die
Geräteeinstellungen gemeinsam genutzt werden. Drittanbieter-Tests benötigen
gezielt ausgewählte lokale Plugins; siehe [Teststand](docs/RELEASE_TESTS_2026-10-20.md).
Alte Sessions und generierte Plugins aus der früheren Cloudes-Version werden
weiter unterstützt. Dafür notwendige alte Dateiformat- und Migrationsbezeichner
bleiben intern erhalten.

## Lizenz und Mitwirkung

Eigener Aizana-Code: **GPL-3.0-or-later**. Die Kombination mit JUCE verwendet den
AGPLv3-Open-Source-Weg; Tracktion und weitere Komponenten behalten ihre eigenen
Lizenzen. Siehe [LICENSE.md](LICENSE.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
und [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md) zu Quellcode und Pflichten.
Eigene Songs werden durch die Verwendung einer GPL/AGPL-DAW nicht automatisch
unter deren Softwarelizenz gestellt.


### Warum steht bei manchen Abhängigkeiten „Commercial“?

JUCE und Tracktion bieten jeweils zwei alternative Lizenzwege. **Aizana verwendet
hier den Open-Source-Weg:** JUCE unter AGPLv3, Tracktion unter GPL-3.0-or-later.
Die zusätzliche kommerzielle Lizenzoption des jeweiligen Herstellers wird für
diesen Weg nicht benötigt. Sie ist keine Kaufpflicht für Aizana-Nutzende.
Die Original-Lizenztexte werden vollständig erhalten, einschließlich der dort
beschriebenen Alternativen.

| Bestandteil | Hier verwendete Lizenz |
|---|---|
| Eigener Aizana-Code | GPL-3.0-or-later |
| JUCE 8 | AGPLv3 |
| Tracktion Engine | GPL-3.0-or-later |
| MVSep-Mega-Modellgewichte und relevante Modellimplementierung | MIT, konkrete Herkunft und Änderungen siehe Modellbericht |
| ONNX Runtime, libebur128, CLAP und weitere Komponenten | MIT sowie enthaltene Fremdhinweise |
| SoundTouch | LGPL-2.1-or-later |
| Eigen | MPL-2.0 sowie enthaltene Fremdhinweise |
| Catch2 (Tests) | BSL-1.0 |

Dies ist die Übersicht, nicht der Ersatz für die vollständigen Texte in
[licenses/](licenses/), [COPYING](COPYING), [COPYING.AGPL](COPYING.AGPL) und
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Bei Weitergabe oder Änderungen
müssen insbesondere Lizenz- und Urheberhinweise, Änderungsvermerke und die
jeweiligen Quellcodepflichten erhalten beziehungsweise erfüllt werden.
Apple-Buildwerkzeuge, optionale KI-Dienste und selbst installierte Plugins werden
separat bezogen und nicht durch Aizanas Projektlizenz mitlizenziert.

Externe Pull Requests werden derzeit nicht angenommen. Bugreports mit
Versionsnummer und reproduzierbaren Schritten sind willkommen. Eigene Forks und
Änderungen sind im Rahmen der Lizenz erlaubt. Technische Hinweise stehen in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Fehler melden und die Alpha ausprobieren

Für einen nachvollziehbaren Fehlerbericht sind die Aizana-Version bzw. der
verwendete Commit, macOS-Version, Audiogerät und die Schritte bis zum Fehler
hilfreich. Bei einem Pluginproblem gehören Format und Pluginversion dazu.
Beschreibe, was du erwartet hast und was tatsächlich passiert ist.

Prüfe Screenshots, Protokolle und Beispielsessions vor dem Teilen auf persönliche
Angaben. Verwende nur Audiomaterial, das du dafür weitergeben darfst. Zugangsdaten,
Lizenzschlüssel und private Aufnahmen gehören nicht in öffentliche Fehlerberichte.
Die [Bedienungsanleitung](MANUAL.md) enthält eine erste Fehlerhilfe.

## Entwicklungshistorie

Dieses Repository übernimmt die bereinigte Entwicklungshistorie des bisherigen
Aizana-Projekts. Das neue GitHub-Erstellungsdatum bezeichnet den Umzug, nicht den
Beginn der Entwicklung. Persönliche Angaben wurden entfernt; Entwicklungsschritte
und ursprüngliche Commit-Zeitstempel bleiben nachvollziehbar.
