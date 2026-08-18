#pragma once

#include <juce_core/juce_core.h>

/**
    SessionPaths — wo die Bestandteile einer Session liegen.

    ## Warum es das gibt

    Bis 2026-08-18 war eine Aizana-Session EINE Datei, und alles, was dazugehoerte,
    landete daneben: Aufnahmen im Verzeichnis der Session (bei einem Anwender also
    im WURZELVERZEICHNIS seiner SSD, zwischen fremden Ordnern), Sicherungen
    gleichwertig daneben, Comps im Papierkorb des Systems. Der Anwender hat die
    losen Aufnahmen aufgeraeumt — voellig zu Recht, nichts daran sah nach
    Projektinhalt aus — und damit seine Takes verloren.

    Der Weg der Referenzen (Studio One legt einen Song-Ordner an, Logic ein Paket):
    **ein Ordner traegt den Namen der Session und enthaelt alles.**

        Mein Song/
            Mein Song.tracktion          <- die Session selbst
            Mein Song Audio/             <- Aufnahmen, darin Comps/ und Bounces/
            Sicherungen/                 <- Autosave-Staende + Wiederherstellung

    ## Warum reine Funktionen

    Die Regeln haben Kanten, die man einmal festschreiben will statt sie bei jedem
    Speichern neu zu erraten: Was, wenn der Anwender im Dialog einen Pfad waehlt,
    der schon in einem gleichnamigen Ordner liegt (= erneutes Speichern derselben
    Session)? Dann darf NICHT nochmal geschachtelt werden. Was, wenn er eine alte,
    flache Session speichert? Die soll weiterlaufen, ohne dass ihr etwas passiert.

    Deshalb steht die Rechnung hier ohne Dateisystem-Zugriff und ist golden-testbar
    ([sessionpaths]); das Anlegen der Ordner passiert getrennt davon.
*/
namespace aizana::sessionpaths
{
    /** Endung der Session-Datei (der Dateidialog filtert darauf). */
    inline const char* sessionExtension() { return ".tracktion"; }

    /** Ordnername fuer Sicherungen INNERHALB des Session-Ordners. */
    inline const char* backupFolderName() { return "Sicherungen"; }

    /** Die Session-Datei, die aus einer Dialog-Auswahl werden soll.

        `chosen` ist, was der Anwender im „Speichern unter"-Dialog eingegeben hat,
        z. B. `/Volumes/SSD/Mein Song.tracktion`. Ergebnis:
        `/Volumes/SSD/Mein Song/Mein Song.tracktion`.

        **Nicht doppelt schachteln:** liegt `chosen` bereits in einem Ordner
        gleichen Namens (`…/Mein Song/Mein Song.tracktion`), bleibt der Pfad wie
        er ist. Sonst entstuende beim zweiten Speichern
        `Mein Song/Mein Song/Mein Song.tracktion`.

        Es wird NICHTS angelegt — das ist reine Rechnung. */
    juce::File sessionFileForChosenPath (const juce::File& chosen);

    /** Der Ordner, der die Session enthaelt (= ihr Verzeichnis). */
    juce::File sessionFolder (const juce::File& sessionFile);

    /** `<Session-Ordner>/<Name> Audio/` — Aufnahmen, darin Comps/ und Bounces/. */
    juce::File mediaFolder (const juce::File& sessionFile);

    /** `<Session-Ordner>/Sicherungen/`. */
    juce::File backupFolder (const juce::File& sessionFile);

    /** Name eines rotierenden Sicherungsstandes (1-basiert). */
    juce::String backupFileName (const juce::File& sessionFile, int slot);

    /** Die Wiederherstellungs-Datei — EIN fester Name, wird laufend ueberschrieben.
        Sie ist das Netz gegen den Absturz und hat mit der Rotation nichts zu tun. */
    juce::File recoveryFile (const juce::File& sessionFile);

    /** Aus einem Dateinamen den Sessionnamen zurueckrechnen.

        Aizana hat bis 2026-08-18 Sicherungen als `<Name>_autosave_N` NEBEN die
        Session gelegt. Sie sahen aus wie Sessions, waren neuer und standen in
        jedem Dialog obenauf — ein Anwender hat prompt eine geoeffnet und darin
        weitergearbeitet. Die naechste Sicherung hiess dann
        `<Name>_autosave_1_autosave_2`: eine Kette, die real auf seiner Platte
        entstanden ist. Wer eine Sicherung oeffnet, soll wieder auf dem STAMM
        landen, nicht eine Stufe tiefer. */
    juce::String sessionStemFrom (const juce::File& file);
}
