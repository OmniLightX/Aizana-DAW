// test_session_state.cpp — Session-Zustand ueber Speichern und Oeffnen.
//
// ANLASS (User-Befund 2026-08-18): "Ich hatte Spuren gemutet und einen
// Loop-Bereich gezogen. Session gespeichert, wieder aufgemacht — die Mutes sind
// weg und der Loop sitzt woanders."
//
// Dieser Test hat den Verdacht WIDERLEGT: Mute und Loop-Bereich ueberleben den
// Weg durch die Datei einwandfrei. Damit war klar, dass der Fehler nicht im
// Speichern/Laden liegt, sondern in der ANZEIGE — und genau dort war er auch:
//   * TrackHeader rief `refresh()` im Konstruktor NICHT auf, ein frisch
//     gebauter Spurkopf zeigte M/S deshalb immer aus (und beim Laden werden
//     alle Spurkoepfe neu gebaut).
//   * Das Lineal fuehrte seinen Loop-Bereich NUR selbst (Vorgabe 1,0-5,0 s) und
//     las ihn nie aus der Session.
//
// Der Test bleibt als Riegel gegen die andere Haelfte: sollte die Persistenz
// spaeter doch brechen, faellt es hier auf und nicht erst beim Anwender.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/EditManager.h"

TEST_CASE ("Session: Mute und Loop-Bereich ueberleben Speichern+Oeffnen", "[persist][probe]")
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("aizana_persist_probe.tracktionedit");
    tmp.deleteFile();

    {
        EditManager em;
        em.createNewEdit();
        auto& edit = *em.getEdit();

        // Zweite Spur anlegen, damit „eine gemutet, eine nicht" pruefbar ist.
        edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

        auto tracks = te::getAudioTracks (edit);
        REQUIRE (tracks.size() >= 2);

        tracks[1]->setMute (true);
        edit.getTransport().setLoopRange ({ tracktion::TimePosition::fromSeconds (4.0),
                                            tracktion::TimePosition::fromSeconds (8.0) });
        edit.getTransport().looping = true;

        CHECK (tracks[1]->isMuted (false));
        em.saveEdit (tmp);
    }

    {
        EditManager em;
        REQUIRE (em.loadEdit (tmp));
        auto& edit = *em.getEdit();
        auto tracks = te::getAudioTracks (edit);
        REQUIRE (tracks.size() >= 2);

        CHECK (tracks[1]->isMuted (false));                                  // <- Befund des Users
        CHECK (edit.getTransport().getLoopRange().getStart().inSeconds() == Catch::Approx (4.0));
        CHECK (edit.getTransport().getLoopRange().getEnd().inSeconds()   == Catch::Approx (8.0));
        CHECK (edit.getTransport().looping.get());
    }

    tmp.deleteFile();
}

//==============================================================================
// Aufnahmen, Comps und Bounces gehoeren zur SESSION — nicht lose daneben.
//
// ANLASS (User-Befund 2026-08-18, mit Bild): auf seiner SSD lagen im
// WURZELVERZEICHNIS Dateien namens "_Track 8_Take_1.wav" — zwischen seinen
// Logic- und Studio-One-Ordnern. Er hat sie aufgeraeumt, weil nichts daran nach
// Projektinhalt aussah, und damit waren die Takes seiner Comping-Aufnahme weg
// ("Quelldatei nicht gefunden" in jeder Take-Spur).
//
// Ursache: TE bestimmt das Aufnahmeziel ueber `WaveInputDevice::filenameMask`.
// Deren Vorgabe beginnt mit %projectdir%, und ohne Project-Verwaltung (I-07)
// faellt das auf das Verzeichnis der Edit-Datei zurueck — bei einer Session im
// Wurzelverzeichnis also dorthin. Der fuehrende Unterstrich kam von %edit%:
// `Edit::getName()` liest den Namen aus dem ProjectItem, das es hier nie gibt.

TEST_CASE ("Session: Aufnahmen landen im Medienordner der Session", "[persist][media]")
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("aizana_media_probe");
    dir.deleteRecursively();
    dir.createDirectory();

    auto sessionFile = dir.getChildFile ("Mein Lied.tracktion");

    EditManager em;
    em.createNewEdit();
    em.saveEdit (sessionFile);

    REQUIRE (sessionFile.existsAsFile());

    // 1. Der Medienordner haengt an der Session und traegt ihren Namen.
    const auto media = em.mediaDirectory();
    CHECK (media.getFileName() == "Mein Lied Audio");
    CHECK (media.getParentDirectory() == dir);
    CHECK (media.isDirectory());                       // wird angelegt

    // 2. Das Aufnahmeziel zeigt DORTHIN — und nicht mehr neben die Session.
    const auto mask = em.recordingFilenameMask();
    INFO ("Maske: " << mask);

    CHECK (mask.startsWith (media.getFullPathName()));

    // Der Dateiname traegt den Sessionnamen — kein fuehrender Unterstrich mehr,
    // dem man nicht ansieht, wozu er gehoert. Und KEIN %edit%: das bliebe leer.
    CHECK (mask.contains ("Mein Lied_%track%_Take_%take%"));
    CHECK_FALSE (mask.contains ("%edit%"));

    // Gegenkontrolle: die alte Ablage lag DIREKT neben der Session. Genau das
    // darf nicht mehr passieren.
    CHECK_FALSE (juce::File (mask).getParentDirectory() == dir);

    // 3. Ohne gespeicherte Session trotzdem ein benannter, SCHREIBBARER Ort —
    //    nie das Arbeitsverzeichnis (TEs Fallback), nie der Systempapierkorb.
    {
        EditManager frisch;
        frisch.createNewEdit();
        const auto ungespeichert = frisch.mediaDirectory();
        INFO ("Ungespeichert: " << ungespeichert.getFullPathName());
        CHECK (ungespeichert.getFileName().endsWith (" Audio"));
        CHECK (ungespeichert.getParentDirectory().getFileName() == "Unsaved Recordings");
    }

    dir.deleteRecursively();
}
