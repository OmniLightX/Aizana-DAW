# Aizana 0.3.0 Alpha — Entwicklungs- und Teststand

Stand: 20. Oktober 2026. Aizana wird als Quellcode zum Selbstbauen bereitgestellt.
Build-Ziel: Apple Silicon (arm64), macOS 13.3 oder neuer.

## Bestätigte Prüfungen

- Debug und Release: jeweils 444 Engine-Testfälle und 49.837 Assertions bestanden.
  Sechs Fälle wurden zunächst übersprungen. Die optionalen VST3-/CLAP-Prüfungen
  wurden mit installierten Plugins nachgeholt: je Konfiguration acht ausgewählte
  Fälle und 35 Assertions bestanden, ohne Skips. Ein plattformfremder Fall und
  ein Fall mit nicht verfügbarem MIDI-Gerätesystem bleiben ausgenommen.
- Kein JUCE-Assertion- oder Leak-Bericht in diesen Läufen. Der separate
  OOP-Smoke-Test besteht in beiden Konfigurationen.
- Fresh Air: nur das native Pluginfenster sichtbar, Regler bedienbar;
  Öffnen, Schließen, Wiederöffnen und Sessionwechsel geprüft. Der vorherige
  Plugin-Prozess wird beim Wechsel beendet.
- Spur anlegen, Undo/Redo, Speichern und Wiederöffnen geprüft. Ein nicht mehr
  beschreibbares Speicherziel meldet einen Fehler und erhält die vorhandene Datei.
- Einzel-Insert mit Option zwischen zwei Audiokanälen verschieben und ohne
  Option auf einen Bus kopieren geprüft. Parameterzustand und Undo/Redo sind
  für die dokumentierten Fälle nachgewiesen.
- App-Builds und Bundle-Audit bestanden. Modell, Lizenzdatei und Prüfsummenliste
  wurden vollständig heruntergeladen und mit den Ausgangsdateien verglichen.

## Noch nicht vollständig abgenommen

- Anonymer Modellabruf und Erstdownload über den regulären App-Dialog.
- Physisches MIDI-Keyboard und Hardware-Cue-Ausgänge.
- Weitere Drittanbieter-OOP-Editoren und deren Klangvergleich; die Fresh-Air-
  Abnahme ist keine Zusage für jedes Plugin.
- Noch nicht einzeln belegte Insert-Kombinationen und visuelle Toggle-Zustände;
  bereits bestandene Ketten-/Preset-Prüfungen stehen in der Live-Übersicht.
- Erststart auf einem unabhängigen Mac. Kein notarisiertes App-Paket verfügbar.

## Dokumentation

- [Bedienungsanleitung](MANUAL.md)
- [Aktuelle Live-Prüfungen](docs/LIVE_TEST_CHECKLIST.md)
- [OOP-Fenster und Regressionstests](docs/OOP_EDITOR_WINDOWS_2026-10-20.md)
- [JUCE-Diagnosen und Korrekturen](docs/DEBUG_DIAGNOSTICS_2026-10-19.md)
- [Modellherkunft und Prüfungen](docs/OPEN_STEM_RELEASE.md)
- [Komponenten und Lizenzen](THIRD_PARTY_NOTICES.md)

KI-Mixing verwendet eingebaute Analyse- und Mischregeln. Die optionale
Stem-Trennung verwendet das dokumentierte MIT-Modell. Die Gesamtanwendung ist
nicht MIT-lizenziert; insbesondere GPL- und AGPL-Bedingungen gelten zusätzlich.
