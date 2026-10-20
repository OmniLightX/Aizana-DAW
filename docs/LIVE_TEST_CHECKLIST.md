# Aizana — Live-Prüfstand

Stand: 20. Oktober 2026. Diese Übersicht unterscheidet abgeschlossene
Bedienprüfungen von noch nicht abgenommenen Kombinationen. Automatisierte Tests
und native Bedienprüfungen sind unterschiedliche Nachweise.

## Aktuelle Bedienprüfungen

| Bereich | Ergebnis | Nachweis |
|---|---|---|
| OOP-Editor Fresh Air | Bestanden: ein natives Fenster, bedienbare Regler, Wiederöffnung und Sessionwechsel mit beendetem altem Worker | [OOP-Fensterprüfung](OOP_EDITOR_WINDOWS_2026-10-20.md) |
| Einzel-Insert verschieben | Bestanden: Option-Ziehen zwischen Audiokanälen, Zustand erhalten, Undo/Redo | [Insert-Prüfung](INSERT_FEEDBACK_2026-10-19.md) |
| Einzel-Insert kopieren | Bestanden: EQ auf Bus kopiert, Quelle erhalten, eigene Zielinstanz | [Insert-Prüfung](INSERT_FEEDBACK_2026-10-19.md) |
| Spur-Anlage und Session | Bestanden: Anlegen, Undo/Redo, Speichern/Wiederöffnen und Sessionwechsel | [Session-Prüfungen](RELEASE_TESTS_2026-10-20.md) |
| Speicherfehler | Bestanden: nicht beschreibbares Ziel wird gemeldet, vorhandene Datei bleibt erhalten | [Session-Prüfungen](RELEASE_TESTS_2026-10-20.md) |
| Stem-Trennung | Bestanden: Vorschau, Import, Undo/Redo, Abbruch und Sessionwechsel ohne verspäteten Import | [Stem-Prüfungen](OPEN_STEM_RELEASE.md) |
| Mehrfachauswahl und Mixer | Bestanden: Auswahlaktionen, Gruppenfader-Undo und Pan-Anzeige nach Reload | [Auswahl](TEST_HISTORY.md), [Mixer](TEST_HISTORY.md) |

## Weitere bestätigte Insert-Prüfungen

Der [Prüflauf vom 19. Oktober](RELEASE_FINAL_CHECK_2026-10-19.md) belegt außerdem:

- EQ-/Kompressorkette auf Bus und FX kopiert; Reihenfolge sowie Undo/Redo geprüft.
- Bus-Effekte gemeinsam deaktiviert und per Undo wiederhergestellt.
- FX-Effekte entfernt und per Undo mit ursprünglichen IDs wiederhergestellt;
  Aux-Return erhalten.
- Zwischenablage angehängt bzw. ausdrücklich ersetzt; Undo stellt den Ausgang her.
- Default-Preset auf Master sowie Preset-Datei auf Audiokanal geladen;
  Reihenfolge, eigene Instanzen und Session-Wiederöffnung geprüft.
- Bus-Mute und FX-Solo ändern den Engine-Zustand korrekt, einschließlich Undo.

## Abgeschlossener funktionaler Prüflauf

Die folgenden 21 Prüfungen wurden zwischen dem 11. und 15. Oktober abgeschlossen.
Sie sind keine Gesamtzählung der automatisierten Tests.

| Kennung | Konkrete Restprüfung | Status |
|---|---|---|
| ORD3 | Automatische Sicherungen im Backups-Ordner | Erledigt: drei gültige Sicherungen samt Audioverweis |
| ORD4 | Kontrollierter Absturz und Wiederherstellung | Erledigt: Dialog, Inhalt und korrigierter Speicherweg geprüft |
| MED1 | Neue Aufnahme im benannten Audioordner | Erledigt: Mikrofonaufnahme, WAV und Hörbestätigung |
| ORD7 | Alte flache Session und deren Backups | Erledigt: Testkopie geladen, Wellenformen/Transport/Pegel geprüft; Wiederherstellung und rotierender Autosave im benachbarten Backups-Ordner, je 16 Spuren und 14 gültige Audioverweise. Siehe TEST_HISTORY.md |
| MED2 | Comp-Datei im Audio/Comps-Ordner | Erledigt: viersekündige Comp-WAV im eigenen Audio/Comps-Ordner; neun Abschnittsmittelpunkte samplegenau mit ausgewählten Takes verglichen |
| MED3 | Bounce-Datei im Audio/Bounces-Ordner | Erledigt: Bounce nur Event in Test-App erstellt; WAV mit Signal, identischer Länge, gültigem Session-Verweis; Undo/Redo und Wiederöffnen bestanden. Siehe TEST_HISTORY.md |
| MED4 | Aufnahme vor dem ersten Speichern | Erledigt: Aufnahme nach Gerätewechsel und Vorzähler in korrekter unbenannter Ablage; erstes Speichern übernimmt WAV bytegleich in eigenen Audioordner, Wiederöffnen mit Wellenform und laufender Wiedergabe geprüft. Siehe TEST_HISTORY.md |
| CO1 | Zusammengesetzte Wellenform im Arrangement | Erledigt: Arrangement und Dock zeigen effektiven Comp nach Laden, Rückwechsel und Einklappen; Save erhält aktive Quelle |
| CO3 | Wellenform ändert sich beim Take-Wechsel | Erledigt: Take 1 über Lane und Rückkehr per Comp anhören live; Undo/Redo korrekt. Bedienprüfung bestätigt Take 2 und Rückkehr zum Comp über Menü. Zusammenstellung bleibt erhalten |
| S3 | Solo speichern und wieder öffnen | Erledigt: Solo im Mixer gesetzt, per Cmd+S geschrieben, Original wieder geöffnet und Solo-Anzeige bestätigt |
| S4 | Geladenen Loop ohne Positionssprung weiterziehen | Erledigt: geladener Bereich 0,5–4,5 s  auf 1–5 s verschoben, linke Grenze auf 1,5 s, rechte auf 5,5 s; Bereich wieder auf 0,5–4,5 s zurückgestellt. Kein Sprung zum Songanfang. Siehe TEST_HISTORY.md |
| V18 | Metronom-Einstellungen speichern und wieder öffnen | Erledigt: Metronom an, Vorzähler/Pre-Roll je 2, Aufnahme-Klick aus, Betonung an, Lautstärke 0,35 gesetzt, gespeichert und nach Wiederöffnen  bestätigt. Siehe TEST_HISTORY.md |
| T | Instrumentenspur per Tastatur spielen und aufnehmen | Bestanden 2026-10-14: Bedienprüfung bestätigt Spiel, Aufnahme und Wiedergabe; 144 Noten vorhanden. REC, Darstellung, Doppelklick, Löschen/Undo/Redo, Verschieben, Verlängern und Speichern/Wiederöffnen live bestätigt. Vollsuite 414 bestanden/11 übersprungen. Siehe `TEST_HISTORY.md`. |
| B3 | Synth-Regler, Hüllkurve und Polyphonie | Bestanden 2026-10-14: Live-Regler/Wellenformauswahl und technische Audioabnahme mit Sample-, Hüllkurven- und Vierstimmenvergleich; 9 Synth-Fälle/110 Assertions grün. Kein subjektiver Hörtest behauptet. Siehe `TEST_HISTORY.md`. |
| B4 | Export gegen Wiedergabe und zweiten Export | Bestanden 2026-10-15: erste zwei Exporte PCM-bitgleich; nach Entmuten erneuter Export Vergleich 3. Bedienprüfung bestätigt identischen Klang gegenüber Projektwiedergabe. Siehe `TEST_HISTORY.md`. |
| B6 | KI-Ziel bei einer Spur namens Master | ✅ Live 2026-10-15: EQ und Kompressor nur auf normaler Spur Master; Summenkanal und Kontrolle unverändert. |
| C1 | Tempo-Map vor einem zeitversetzten Clip | Bestanden 2026-10-15: 27 Tempo-Punkte live erzeugt, Grundtempo vor Clip 120,2 BPM; 48 Anschläge maximal 5,98 ms vom Raster, Map und Clipposition gespeichert. 2 Fälle/22 Assertions grün. |
| C2 | Warnung bei sparsamer Kick-Spur, Gegenprobe Drum-Summe | Bestanden 2026-10-15 mit synthetischen Rhythmussignalen: dünn 45 % Warnung (Schwelle 60 %), dicht ohne Warnung 11 Tempo-Punkte/120,2 BPM. |
| C3 | Konkrete Ursache einer Vocal-Tuning-Ablehnung | Bestanden 2026-10-15: untonaler Testclip meldet live ausdrücklich keine erkennbare Tonhöhe, keine pauschale Liste anderer Ablehnungsursachen. Automatische C3-Fälle ebenfalls grün. |
| ORD2-Nachprüfung | Speichern unter im bestehenden Ordner ohne doppelte Schachtelung | Erledigt: zweimal Speichern unter, zweites Mal im bestehenden Ordner mit Ersetzen; gültige Sessiondatei, Audio und Backups, keine doppelte Schachtelung |
| ORD5-Tastenkürzel | Speichern ausdrücklich über Cmd+S | Erledigt: Cmd+S verdrahtet; erste Zielwahl samt Abbrechen geprüft, danach Mute/Solo aus Arrangement/Mixer in derselben Datei nachgewiesen; Cmd+Option+S separat. Siehe TEST_HISTORY.md |

## Noch nicht abgenommen

- Öffentlicher Modell-Erstdownload über den regulären App-Dialog.
- Physisches MIDI-Keyboard und Cue-Ausgänge an einem Mehrkanal-Interface.
- Weitere OOP-Drittanbieter-Editoren einschließlich Klangvergleich.
- Noch nicht einzeln belegte Insert-Kombinationen: ganze Ketten mit Option
  verschieben, sämtliche Zielkanäle, Umordnung und alle visuellen Toggle-Zustände.
  Die oben belegten Ketten-, Preset- und Bulk-Prüfungen bleiben bestanden.
- Drittanbieter-Presets und Sidechain-Konfigurationen mit Undo/Redo in der nativen UI.
- Vollständiger Bedienvergleich und Lokalisierung aller Dialoge.

Die aktuellen automatisierten Ergebnisse stehen in [STATUS.md](../STATUS.md).
Diese Liste behauptet weder ungetestete Prüfungen als bestanden noch eröffnet sie
bereits belegte Einzelprüfungen erneut.
