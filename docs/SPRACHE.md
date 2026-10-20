# Language — German UI and English documentation

Aizana currently ships with a German user interface, as Logic and Studio One do
on a German system. There is currently no UI language switch.

The primary project documentation is now English. German versions remain
available as [README.de.md](../README.de.md) and [MANUAL.de.md](../MANUAL.de.md).
This documentation change does not change the application's UI language.

The following conventions keep menus, dialogs and technical terms consistent.

## 1. Translate ordinary language into German

Actions and states: “Speichern”, “Speichern unter…”, “Oeffnen…”, “Neu”,
“Einstellungen…”, “Spur hinzufuegen”, “Stummschalten”, “Rueckgaengig”, “Wiederholen”,
“Importieren”, “Exportieren”, “Wiederherstellen”, “Abbrechen”, “Schliessen” — along
with all dialog text, messages and tooltips.

## 2. Keep terms used in English in German studios

Loop · Take · Bus · Send · Solo · Click · Stems · Mixer · Tempo · Marker ·
Automation · Bounce · Song · Session · Backup · Plug-In

These are studio terms. Studio One also retains them in its German interface.

## 3. Folder and file names on disk are English

This is a separate category, independent of the interface language. Even in its
German version, Studio One creates `Media`, `Mixdown` and `History`, including
where the interface translates the term (“Medien”). Logic creates `Audio Files`
and `Bounces`.

Aizana follows this convention: `<Name> Audio/`, `Comps/`, `Bounces/`, `Backups/`.

File names INSIDE these folders (`<Name>_Sicherung_N`, `<Name>_Wiederherstellung`)
are unaffected. They are deliberately retained because users see them in the
restore dialog, where the text is German.

## 4. If a UI language switch is introduced later

That would require structural changes, not just translation:

* The source language must become **English**. JUCE's `TRANS()` looks up the
  SOURCE TEXT in a table, and Tracktion Engine already supplies its own messages
  with English source text. Only English source text allows ONE German table
  to cover both Aizana and the engine messages.
* Around 358 visible string literals are affected, at least 147 of them
  demonstrably German. Because of rule 2, however, the table would need only
  about 150–250 entries: terms identical in both languages need no entry because
  `TRANS()` falls back to the source text.
* **GUI test automation must pin the language** to German. 209 script lines
  address components by their German names, sometimes including state
  (“Klick bei Aufnahme: An”). Without pinning, the first run would report many
  “not found” errors, sending debugging in the wrong direction. Menu entries
  are unaffected: scripts invoke them by numeric ID, not text.
