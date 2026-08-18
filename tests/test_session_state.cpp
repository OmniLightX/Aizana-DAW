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
