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

## 4. Adding translations

Keep user-facing strings separate from serialized keys and session paths.
JUCE's `TRANS()` looks up source text; a future translation layer must cover
both Aizana and the engine's messages consistently.

GUI tests currently address components by their German labels. Pin the test
language and update label-dependent checks when adding translations. Native
menu tests that use item IDs are separate from those label lookups.
