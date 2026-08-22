// test_session_paths.cpp — wo die Bestandteile einer Session liegen.
//
// ANLASS (User-Befund 2026-08-18): Aufnahmen lagen lose im Verzeichnis der
// Session (bei ihm im Wurzelverzeichnis seiner SSD), Sicherungen gleichwertig
// daneben — er hat sie aufgeraeumt und Takes verloren, und er hatte zuvor schon
// versehentlich eine SICHERUNG geoeffnet und darin weitergearbeitet (Kette
// `<Name>_autosave_1_autosave_2` auf seiner Platte).
//
// Jetzt der Weg von Studio One und Logic: EIN Ordner mit dem Namen der Session
// enthaelt alles. Die Regeln haben Kanten, die hier festgeschrieben sind.

#include <catch2/catch_test_macros.hpp>
#include "engine/SessionPaths.h"

namespace sp = aizana::sessionpaths;

TEST_CASE ("Session-Ordner: aus der Dialog-Auswahl wird ein Ordner", "[sessionpaths]")
{
    const juce::File gewaehlt ("/Volumes/SSD/Mein Song.tracktion");
    const auto datei = sp::sessionFileForChosenPath (gewaehlt);

    CHECK (datei.getFullPathName() == "/Volumes/SSD/Mein Song/Mein Song.tracktion");
    CHECK (sp::sessionFolder (datei).getFullPathName() == "/Volumes/SSD/Mein Song");
    CHECK (sp::mediaFolder (datei).getFullPathName()   == "/Volumes/SSD/Mein Song/Mein Song Audio");
    CHECK (sp::backupFolder (datei).getFullPathName()  == "/Volumes/SSD/Mein Song/Backups");
}

TEST_CASE ("Session-Ordner: wird NICHT doppelt geschachtelt", "[sessionpaths]")
{
    // Der zweite „Speichern unter"-Durchgang auf dieselbe Session. Ohne diese
    // Regel entstuende Mein Song/Mein Song/Mein Song.tracktion.
    const juce::File schonDrin ("/Volumes/SSD/Mein Song/Mein Song.tracktion");
    CHECK (sp::sessionFileForChosenPath (schonDrin) == schonDrin);
}

TEST_CASE ("Sicherungen liegen im Unterordner und heissen nach der Session", "[sessionpaths]")
{
    const juce::File datei ("/Volumes/SSD/Mein Song/Mein Song.tracktion");

    CHECK (sp::backupFileName (datei, 2) == "Mein Song_Sicherung_2.tracktionedit");
    CHECK (sp::recoveryFile (datei).getFullPathName()
             == "/Volumes/SSD/Mein Song/Backups/Mein Song_Wiederherstellung.tracktionedit");
}

TEST_CASE ("Aus einer Sicherung wird wieder der Sessionname — auch aus der Kette", "[sessionpaths]")
{
    // Dieselbe Form wie die Dateinamen, die real auf der Platte des Anwenders
    // lagen (Name hier neutralisiert).
    CHECK (sp::sessionStemFrom (juce::File ("/x/Alte Session_autosave_1.tracktionedit"))
             == "Alte Session");
    CHECK (sp::sessionStemFrom (juce::File ("/x/Alte Session_autosave_1_autosave_2.tracktionedit"))
             == "Alte Session");
    CHECK (sp::sessionStemFrom (juce::File ("/x/Mein Song_Sicherung_3.tracktionedit"))
             == "Mein Song");
    CHECK (sp::sessionStemFrom (juce::File ("/x/Mein Song_Wiederherstellung.tracktionedit"))
             == "Mein Song");

    // Gegenkontrolle: ein Sessionname, der zufaellig aehnlich aussieht, bleibt
    // unangetastet — sonst wuerde die Regel echte Namen verstuemmeln.
    CHECK (sp::sessionStemFrom (juce::File ("/x/Take_autosave_ohne_zahl.tracktion"))
             == "Take_autosave_ohne_zahl");
    CHECK (sp::sessionStemFrom (juce::File ("/x/Sicherung_2.tracktion")) == "Sicherung_2");
}

TEST_CASE ("Eine Sicherung als Auswahl landet wieder im Session-Ordner", "[sessionpaths]")
{
    // Wer versehentlich eine Sicherung oeffnet und dann speichert, soll auf dem
    // STAMM landen und keine neue Kette anfangen.
    const juce::File sicherung ("/Volumes/SSD/Alte Session_autosave_1.tracktionedit");
    CHECK (sp::sessionFileForChosenPath (sicherung).getFullPathName()
             == "/Volumes/SSD/Alte Session/Alte Session.tracktion");
}
