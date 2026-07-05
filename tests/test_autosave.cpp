// test_autosave.cpp — M13: Autosave darf die laufende Wiedergabe nicht unterbrechen.
//
// Befund (User-Realtest 2026-07-05, Loop-Mixing): alle ~2 Minuten setzte der
// komplette Mix für einen Sekundenbruchteil aus. Ursache: performAutosave lief
// über te::EditFileOperations::saveAs — dessen writeToFile() deaktiviert die
// Realtime-Priorität der Audio-Threads und edit.flushState() suspendiert JEDES
// gehostete Plugin (suspendProcessing(true) → Stille) während des State-Reads.
//
// Fix: während der Wiedergabe schreibt performAutosave einen Snapshot des
// ValueTrees über EditManager::writeAutosaveSnapshotAsync (Deep Copy → Background-
// Thread → temp-write + atomic move). Diese Tests decken den Snapshot-Kern und
// den unveränderten Stopped-Pfad ab; das Hörbarkeits-Kriterium selbst ist
// LIVE-TEST-only (docs/LIVE_TEST_CHECKLIST.md, M13).

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"

namespace
{
    juce::File makeTempDir (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_autosave_tests")
                       .getChildFile (name + "_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        dir.createDirectory();
        return dir;
    }

    // Background write is async — poll until the slot holds a parseable tree.
    juce::ValueTree waitForSnapshot (const juce::File& target, int timeoutMs = 5000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (target.existsAsFile())
                if (auto xml = juce::XmlDocument::parse (target))
                    return juce::ValueTree::fromXml (*xml);
            juce::Thread::sleep (20);
        }
        return {};
    }
}

TEST_CASE ("writeAutosaveSnapshotAsync writes a loadable snapshot without touching the edit", "[autosave]")
{
    const auto dir    = makeTempDir ("snapshot");
    const auto target = dir.getChildFile ("session_autosave_1.tracktionedit");

    juce::ValueTree state ("EDIT");
    state.setProperty ("appVersion", "test", nullptr);
    juce::ValueTree track ("TRACK");
    track.setProperty ("name", "Vocals", nullptr);
    state.appendChild (track, nullptr);

    EditManager::writeAutosaveSnapshotAsync (state.createCopy(), target);

    const auto reloaded = waitForSnapshot (target);
    REQUIRE (reloaded.isValid());
    CHECK (reloaded.getType().toString() == "EDIT");
    CHECK (reloaded.getChild (0).getProperty ("name").toString() == "Vocals");

    // Kein .tmp-Rest — temp-write + move muss aufgeräumt haben.
    CHECK (! dir.getChildFile ("session_autosave_1.tracktionedit.tmp").exists());

    dir.deleteRecursively();
}

TEST_CASE ("writeAutosaveSnapshotAsync overwrite keeps the slot consistent", "[autosave]")
{
    const auto dir    = makeTempDir ("overwrite");
    const auto target = dir.getChildFile ("session_autosave_2.tracktionedit");

    juce::ValueTree first ("EDIT");
    first.setProperty ("marker", "one", nullptr);
    EditManager::writeAutosaveSnapshotAsync (first.createCopy(), target);
    REQUIRE (waitForSnapshot (target).isValid());

    juce::ValueTree second ("EDIT");
    second.setProperty ("marker", "two", nullptr);
    EditManager::writeAutosaveSnapshotAsync (second.createCopy(), target);

    const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
    juce::ValueTree latest;
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        latest = waitForSnapshot (target, 500);
        if (latest.isValid() && latest.getProperty ("marker").toString() == "two")
            break;
    }
    CHECK (latest.getProperty ("marker").toString() == "two");

    dir.deleteRecursively();
}

TEST_CASE ("performAutosave (transport stopped) still writes a full autosave slot", "[autosave]")
{
    EditManager em;
    em.createNewEdit();   // sets a real currentSessionFile (Unsaved Recordings)

    REQUIRE (em.getEdit() != nullptr);
    REQUIRE (! em.getEdit()->getTransport().isPlaying());

    em.performAutosave();

    const auto latest = em.getLatestAutosave (em.getCurrentFile());
    REQUIRE (latest.existsAsFile());

    const auto xml = juce::XmlDocument::parse (latest);
    REQUIRE (xml != nullptr);
    CHECK (juce::ValueTree::fromXml (*xml).isValid());
}
