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
#include "audio/AudioEngine.h"
#include "audio/AudioRecorder.h"

TEST_CASE ("Session: recording placeholder does not count as a chosen save destination", "[persist][save-destination]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    REQUIRE (em.getCurrentFile().existsAsFile());
    REQUIRE (em.needsSaveAs());
    const auto recordingPlaceholder = em.getCurrentFile();

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getNonexistentChildFile ("aizana-save-destination", ".tracktion");
    em.saveEdit (file);
    REQUIRE (file.existsAsFile());
    CHECK_FALSE (em.needsSaveAs());
    REQUIRE (em.loadEdit (file, recordingPlaceholder));
    CHECK (em.needsSaveAs());
    em.createNewEdit();
    CHECK (em.needsSaveAs());
    REQUIRE (em.loadEdit (file));
    CHECK_FALSE (em.needsSaveAs());
    file.deleteFile();
    CHECK (em.needsSaveAs());
}

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

//==============================================================================
// Wiederherstellung nach Absturz.
//
// ANLASS (User, 2026-08-18): "Was ist, wenn das Programm einmal abstuerzt? Beim
// Abstuerzen speichern Programme wie Studio One das ja auch immer noch, damit
// man nicht dasitzt und denkt, jetzt ist mein ganzer Stand weg. Autosave bei
// 60 Minuten, Absturz bei 59 — dann sind 59 Minuten Arbeit einfach weg."
//
// Der rotierende Autosave ist die Zeitmaschine, nicht das Netz. Das Netz ist ein
// billiger Schnappschuss im Minutentakt plus eine MARKE, die beim sauberen
// Beenden verschwindet. Nur ihr Ueberleben unterscheidet "abgestuerzt" von
// "normal zugemacht" — und genau das wird hier geprueft.

TEST_CASE ("Absturz: der Schnappschuss traegt die Arbeit, die Marke stellt die Frage",
           "[persist][recovery]")
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("aizana_recovery_probe");
    dir.deleteRecursively();
    dir.createDirectory();

    const auto sessionFile = dir.getChildFile ("Absturztest.tracktion");
    EditManager::clearCrashMarker();

    {
        EditManager em;
        em.createNewEdit();
        em.saveEdit (sessionFile);

        // ... und DANACH weitergearbeitet, ohne zu speichern. Das ist die
        // Arbeit, die bei einem Absturz verloren waere.
        auto& edit = *em.getEdit();
        edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);
        const auto spurenNachher = te::getAudioTracks (edit).size();

        em.writeRecoverySnapshot();

        // Der Schnappschuss wird im Hintergrund geschrieben — kurz warten.
        const auto snapshot = aizana::sessionpaths::recoveryFile (sessionFile);
        for (int i = 0; i < 200 && ! snapshot.existsAsFile(); ++i)
            juce::Thread::sleep (10);

        REQUIRE (snapshot.existsAsFile());
        CHECK (snapshot.getParentDirectory().getFileName() == "Backups");
        // Fast atomic saves can share a filesystem timestamp with recovery.
        // A different snapshot must not be discarded merely because of that tie.
        REQUIRE (snapshot.setLastModificationTime (sessionFile.getLastModificationTime()));

        // Die gespeicherte Session weiss von der neuen Spur NICHTS.
        {
            EditManager gespeichert;
            REQUIRE (gespeichert.loadEdit (sessionFile));
            CHECK (te::getAudioTracks (*gespeichert.getEdit()).size() == spurenNachher - 1);
        }

        // Die Marke sagt: hier ist eine Sitzung offen.
        INFO ("Session mtime: " << sessionFile.getLastModificationTime().toMilliseconds()
              << "; snapshot mtime: " << snapshot.getLastModificationTime().toMilliseconds());
        const auto gefunden = EditManager::findCrashRecovery();
        REQUIRE (gefunden.has_value());
        CHECK (gefunden->session  == sessionFile);
        CHECK (gefunden->snapshot == snapshot);

        // Und der Schnappschuss traegt die Arbeit wirklich.
        EditManager wieder;
        REQUIRE (wieder.loadEdit (gefunden->snapshot, gefunden->session));
        CHECK (te::getAudioTracks (*wieder.getEdit()).size() == spurenNachher);

        // WICHTIG: gespeichert wird an der SESSION, nicht im Sicherungsordner —
        // sonst arbeitet man nach dem Wiederherstellen in einer Sicherung
        // weiter (genau die Falle, in die der Anwender schon einmal gelaufen ist).
        CHECK (wieder.getCurrentFile() == sessionFile);
        SECTION ("Save immediately after recovery") {}
        SECTION ("Save after delayed engine initialisation")
        {
            // The running app dispatches TE's delayed post-load dirty reset.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        }

        // A recovered snapshot is unsaved work relative to the original file.
        // Saving immediately (without another edit) must persist that work.
        wieder.saveEdit (sessionFile);
        CHECK_FALSE (wieder.getEdit()->hasChangedSinceSaved());
        CHECK_FALSE (EditManager::findCrashRecovery().has_value());
        EditManager erneut;
        REQUIRE (erneut.loadEdit (sessionFile));
        CHECK (te::getAudioTracks (*erneut.getEdit()).size() == spurenNachher);
    }

    // Sauber beendet = keine Frage beim naechsten Start.
    EditManager::clearCrashMarker();
    CHECK_FALSE (EditManager::findCrashRecovery().has_value());

    dir.deleteRecursively();
}

TEST_CASE ("Absturz: nach dem Speichern gibt es nichts zu fragen", "[persist][recovery]")
{
    // Gegenkontrolle. Wer nach dem letzten Schnappschuss noch gespeichert hat,
    // verliert nichts — dann darf die Frage NICHT kommen. Eine Frage, die man
    // immer wegklickt, schuetzt niemanden mehr.
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("aizana_recovery_probe2");
    dir.deleteRecursively();
    dir.createDirectory();

    const auto sessionFile = dir.getChildFile ("Sauber.tracktion");
    EditManager::clearCrashMarker();

    EditManager em;
    em.createNewEdit();
    em.saveEdit (sessionFile);
    em.writeRecoverySnapshot();

    const auto snapshot = aizana::sessionpaths::recoveryFile (sessionFile);
    for (int i = 0; i < 200 && ! snapshot.existsAsFile(); ++i)
        juce::Thread::sleep (10);
    REQUIRE (snapshot.existsAsFile());

    // Jetzt noch einmal speichern — die Session ist damit juenger.
    juce::Thread::sleep (1100);      // Dateizeiten haben Sekundenaufloesung
    em.saveEdit (sessionFile);

    CHECK_FALSE (EditManager::findCrashRecovery().has_value());

    EditManager::clearCrashMarker();
    dir.deleteRecursively();
}


TEST_CASE ("Recording refreshes a late input device's stale session destination", "[persist][media][recording-destination]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    EditManager em;
    em.createNewEdit();
    auto& engine = em.getEngine();
    auto& dm = engine.getDeviceManager();
    dm.dispatchPendingUpdates();

    // Real TE input objects, but no CoreAudio device: simulate a device appearing
    // AFTER File > New and restoring its per-device destination from an old edit.
    const auto name = "Aizana destination regression " + juce::Uuid().toString();
    const te::WaveDeviceDescription description (name, 0, -1, false);
    const auto staleMask = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("previous-session/Previous_%track%_%take%")
                               .getFullPathName();
    {
        te::WaveInputDevice previous (engine, "test", description, te::InputDevice::waveDevice);
        previous.setFilenameMask (staleMask);
    }
    auto* lateInput = dm.waveInputs.add (new te::WaveInputDevice (
        engine, "test", description, te::InputDevice::waveDevice));
    REQUIRE (lateInput->getFilenameMask() == staleMask);

    AudioEngine audio (engine); // Deliberately not initialised: no hardware needed.
    AudioRecorder recorder (em, audio);
    recorder.startRecording();
    CHECK (lateInput->getFilenameMask() == em.recordingFilenameMask());
    CHECK (lateInput->getFilenameMask().contains ("Unsaved Recordings"));
    recorder.stopRecording();
}
