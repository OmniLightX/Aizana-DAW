// test_instrument_tracks.cpp — Instrumentenspuren + virtuelles Keyboard.
//
// User-Befund 2026-08-15 beim Synth-Test, zwei Luecken:
//   "Wo finde ich denn den MIDI-Clip?"  — es gab keine Instrumentenspur; man
//   legte eine Audiospur an und haengte ein Instrument hinein. Die App erkannte
//   so eine Spur selbst nicht als MIDI-Spur (sie riet am INHALT statt am Typ).
//   "Wenn ich aufnehmen moechte, wird die Melodie nicht aufgenommen." — die
//   Bildschirmklaviatur spielte ueber injectLiveMidiMessage direkt in den
//   WIEDERGABE-Graphen und kam nie am Aufnahmepfad vorbei.
//
// Referenz: Studio One "To record MIDI data you will create an Instrument
// track"; Logic nimmt ausdruecklich auch vom "Musical Typing window" auf.

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/InstrumentTracks.h"

TEST_CASE ("Instrumentenspur: entsteht mit Merkmal, Instrument und MIDI-Clip", "[instrument][p0]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();
    REQUIRE (edit != nullptr);

    const int before = te::getAudioTracks (*edit).size();

    bool instrumentEingesetzt = false;
    auto* track = aizana::createInstrumentTrack (*edit, "Synth 1",
                                                 [&] (te::AudioTrack&) { instrumentEingesetzt = true; });

    REQUIRE (track != nullptr);
    CHECK (instrumentEingesetzt);
    CHECK (track->getName() == "Synth 1");

    // Der Typ steht am MERKMAL, nicht am Inhalt — das war der Kern des Befunds.
    CHECK (aizana::isInstrumentTrack (*track));
    CHECK (track->state.getProperty (aizana::kTrackTypeProp).toString() == aizana::kInstrumentTag);

    // Genau EIN Clip, und zwar ein MIDI-Clip: sonst gibt es nichts zum
    // Doppelklicken und der Piano Roll bleibt unerreichbar.
    REQUIRE (track->getClips().size() == 1);
    CHECK (dynamic_cast<te::MidiClip*> (track->getClips()[0]) != nullptr);

    // Vier Takte bei 120 BPM / 4-4 = 8 s.
    const double len = track->getClips()[0]->getPosition().getLength().inSeconds();
    CHECK (len > 7.9);
    CHECK (len < 8.1);

    // Ans ENDE eingefuegt (I-06) — Spurnummern duerfen sich nicht verschieben.
    auto tracks = te::getAudioTracks (*edit);
    CHECK (tracks.size() == before + 1);
    CHECK (tracks[tracks.size() - 1] == track);

    // Ein Undo nimmt die ganze Geste zurueck.
    em.getUndoManager().undo();
    CHECK (te::getAudioTracks (*edit).size() == before);
}

TEST_CASE ("Instrumentenspur: eine reine Audiospur ist keine", "[instrument][p0]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();

    auto* audio = te::getAudioTracks (*edit)[0];
    REQUIRE (audio != nullptr);
    CHECK_FALSE (aizana::isInstrumentTrack (*audio));

    // GEGENKONTROLLE zum Rueckfall-Zweig: eine ALTE Spur ohne Merkmal, aber mit
    // MIDI-Clip, muss weiterhin als Instrumentenspur gelten — sonst verloeren
    // bestehende Sessions beim Oeffnen ihren Eingang.
    te::ClipPosition pos { tracktion::core::TimeRange (
                               tracktion::core::TimePosition::fromSeconds (0.0),
                               tracktion::core::TimePosition::fromSeconds (2.0)),
                           tracktion::core::TimeDuration() };
    audio->insertNewClip (te::TrackItem::Type::midi, "Alt", pos, nullptr);

    CHECK (aizana::isInstrumentTrack (*audio));
    CHECK (audio->state.getProperty (aizana::kTrackTypeProp).toString().isEmpty());
}

TEST_CASE ("Virtuelles Keyboard: Geraet existiert und ist zweimal dasselbe", "[instrument][p0]")
{
    EditManager em;
    em.createNewEdit();
    auto* edit = em.getEdit();

    auto* dev1 = aizana::getOrCreateKeyboardDevice (edit->engine);

    if (dev1 == nullptr)
        SKIP ("Kein MIDI-Geraetesystem in dieser Umgebung");

    CHECK (dev1->getName() == juce::String (aizana::kKeyboardDeviceName));

    // Zweiter Aufruf darf KEIN zweites Geraet anlegen — sonst sammelt die
    // Eingangsliste bei jedem Tastendruck einen weiteren Eintrag an.
    auto* dev2 = aizana::getOrCreateKeyboardDevice (edit->engine);
    CHECK (dev1 == dev2);

    int gefunden = 0;
    for (auto& d : edit->engine.getDeviceManager().getMidiInDevices())
        if (d != nullptr && d->getName() == juce::String (aizana::kKeyboardDeviceName))
            ++gefunden;
    CHECK (gefunden == 1);

    // Noten senden darf nicht abstuerzen und meldet Erfolg.
    CHECK (aizana::sendKeyboardNoteOn  (edit->engine, 60, 100));
    CHECK (aizana::sendKeyboardNoteOff (edit->engine, 60));
}

// ---------------------------------------------------------------------------
// Autosave-Intervall (User-Wunsch 2026-08-15): "Das muellt die Platte voll.
// Der User soll selber bestimmen, wann ein Autosave stattfindet."
//
// Vorher fest verdrahtete zwei Minuten. Jetzt waehlbar und persistent — hier
// wird die Einstellungslogik geprueft, inklusive der Ablehnung unsinniger Werte
// (ein von Hand editierter Wert darf die App nicht in einen Sekundentakt
// schicken).
// ---------------------------------------------------------------------------
TEST_CASE ("Autosave: Intervall ist einstellbar, persistent und gegen Unsinn geschuetzt",
           "[autosave][settings]")
{
    EditManager em;
    em.createNewEdit();

    const int original = em.getAutosaveIntervalMinutes();

    for (int minutes : { 0, 15, 60, 120 })
    {
        em.setAutosaveIntervalMinutes (minutes);
        CHECK (em.getAutosaveIntervalMinutes() == minutes);
    }

    // Ein Wert ausserhalb der bekannten Stufen faellt auf die Vorgabe zurueck.
    em.setAutosaveIntervalMinutes (7);
    CHECK (em.getAutosaveIntervalMinutes() == 5);

    em.setAutosaveIntervalMinutes (-3);
    CHECK (em.getAutosaveIntervalMinutes() == 5);

    // Und der Wert ueberlebt eine zweite Instanz — er liegt in den
    // App-Einstellungen, nicht in der Session.
    em.setAutosaveIntervalMinutes (30);
    {
        EditManager zweite;
        CHECK (zweite.getAutosaveIntervalMinutes() == 30);
    }

    em.setAutosaveIntervalMinutes (original);
}
