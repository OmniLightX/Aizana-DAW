# Sprache — was deutsch wird und was englisch bleibt

Aizana wird deutsch ausgeliefert, so wie Logic und Studio One auf einem deutschen
System deutsch laufen. Eine Umschaltung ist vorerst nicht vorgesehen.

Die Regel unten ist beim Uebersetzen der Menueleiste (2026-08-22) entstanden. Sie
steht hier, damit nicht an einer Stelle „Stummschalten" steht und drei Klicks
weiter „Mute" — genau die Mischung pro Flaeche, die den Anlass gab: im Menue stand
„Save As…", der Dialog danach hiess „Session speichern unter …".

Die Linie ist nicht erfunden, sondern an der deutschen Sprachdatei von Studio
One 7 nachgemessen (`Studio One German.langpack`, gettext-Tabellen).

## 1. Uebersetzt wird, was normale Sprache ist

Handlungen und Zustaende: Speichern, Speichern unter…, Oeffnen…, Neu,
Einstellungen…, Spur hinzufuegen, Stummschalten, Rueckgaengig, Wiederholen,
Importieren, Exportieren, Wiederherstellen, Abbrechen, Schliessen — dazu
saemtliche Dialogtexte, Meldungen und Tooltips.

## 2. Englisch bleibt, was im deutschen Studio so gesagt wird

Loop · Take · Bus · Send · Solo · Click · Stems · Mixer · Tempo · Marker ·
Automation · Bounce · Song · Session · Backup · Plug-In

Das ist Studiojargon. Studio One laesst diese Begriffe im deutschen Programm
ebenfalls stehen.

## 3. Ordner- und Dateinamen auf der Platte sind englisch

Getrennte Kategorie, unabhaengig von der Oberflaechensprache. Studio One legt
auch im deutschen Programm `Media`, `Mixdown` und `History` an — sogar dort, wo
die Oberflaeche uebersetzt („Medien"). Logic legt `Audio Files` und `Bounces` an.

Aizana entsprechend: `<Name> Audio/`, `Comps/`, `Bounces/`, `Backups/`.

Nicht davon beruehrt sind die Dateinamen INNERHALB dieser Ordner
(`<Name>_Sicherung_N`, `<Name>_Wiederherstellung`) — bewusst so belassen; der
Anwender sieht sie im Wiederherstellen-Dialog, dort ist der Text deutsch.

## 4. Falls doch einmal umgeschaltet werden soll

Dann ist es eine Umbauarbeit, keine Uebersetzungsarbeit:

* Die Quellsprache muss **Englisch** werden. JUCEs `TRANS()` schlaegt den
  QUELLTEXT in einer Tabelle nach, und Tracktion Engine liefert seine eigenen
  Meldungen bereits mit englischen Quelltexten. Nur mit englischen Quelltexten
  deckt EINE deutsche Tabelle Aizana und die Engine-Meldungen zugleich ab.
* Betroffen sind rund 358 sichtbare Textliterale, davon mindestens 147
  nachweislich deutsch. Wegen Regel 2 braeuchte die Tabelle aber nur etwa
  150–250 Eintraege: was in beiden Sprachen gleich heisst, bekommt keine Zeile —
  `TRANS()` faellt auf den Quelltext zurueck.
* **Die GUI-Testautomatik muss die Sprache festnageln** (Deutsch). 209
  Drehbuchzeilen sprechen Bauteile ueber ihren deutschen Namen an, teils mit
  Zustand darin („Klick bei Aufnahme: An"). Ohne Pinning meldet der erste Lauf
  danach reihenweise „nicht gefunden", und man sucht den Fehler an der falschen
  Stelle. Menueeintraege sind nicht betroffen — die Drehbuecher rufen sie ueber
  ihre numerische ID auf, nicht ueber den Text.
