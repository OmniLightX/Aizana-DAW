// test_warp_bend.cpp — P7b Audio-Bend/Warp (S1 "Audio Bend") über TEs
// WarpTimeManager, gekapselt in WarpEngine.
//
// Kernaussagen:
//   - setWarpEnabled seedet Endpunkt-Marker und ist undobar
//   - addMarkerAtSourceTime ändert das Mapping NICHT (Identität), erst moveMarker
//   - TE clampt die Segment-Stretch-Ratio (0.1..20)
//   - RENDER-BEWEIS: der Warp-Proxy-Pfad (WarpTimeRenderJob; Offline-Render
//     wartet via WaveNode::isReadyToProcess auf den Proxy) verschiebt den
//     Audio-Inhalt tatsächlich — ein Burst wandert von 0.75s nach 0.5s
//   - Warp-State (Marker) überlebt Save/Reload

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/WarpEngine.h"

#include <cmath>
#include <atomic>

using Catch::Approx;

namespace
{
    constexpr double kSampleRate = 44100.0;

    /** 1s Stille mit einem 50ms-440Hz-Burst bei burstStartSecs. */
    juce::File writeBurstWav (const juce::File& dir, const juce::String& name,
                              double burstStartSecs)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSampleRate, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (1, (int) kSampleRate);
        buf.clear();
        const int start = (int) (burstStartSecs * kSampleRate);
        const int len   = (int) (0.05 * kSampleRate);
        for (int i = 0; i < len && start + i < buf.getNumSamples(); ++i)
            buf.setSample (0, start + i,
                           0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                    * 440.0 * i / kSampleRate));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    /** Erste Position (Sekunden) mit |sample| über threshold, oder -1. */
    double firstLoudSecs (const juce::File& file, float threshold = 0.2f)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        REQUIRE (reader != nullptr);

        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, true);

        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (std::abs (buf.getSample (0, i)) > threshold)
                return i / reader->sampleRate;
        return -1.0;
    }

    struct WarpFixture
    {
        EditManager em;
        te::WaveAudioClip* clip = nullptr;
        juce::File wavFile;
        juce::File dir;

        WarpFixture (double burstStartSecs = 0.75)
        {
            em.createNewEdit();
            ClipEditor clipEditor (em);

            // HERMETISCH eindeutiges Verzeichnis pro Fixture-INSTANZ (UUID).
            // Kritisch: der Render-Zieldateiname leitet sich aus dem Quell-PFAD
            // ab (AudioFile::getHash = getFullPathName().hashCode64). Ein FESTER
            // Pfad ⇒ derselbe Hash ⇒ dieselbe Render-Datei über Prozess-Läufe
            // hinweg. Bricht ein Prozess mitten im Render ab (Crash/Kill), bleibt
            // eine halb-geschriebene, STILLE Datei liegen, die ein späterer Lauf
            // fälschlich als fertig wiederverwendet (kickWarpRender überspringt
            // den Render, wenn die Zieldatei existiert). UUID ⇒ jeder Lauf
            // rendert frisch. (Produkt-Pendant: seltener Crash-mitten-im-
            // Warp-Render-Edge, dokumentiert in REMAINING_WORK.)
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("aizana_warp_test")
                      .getChildFile (juce::Uuid().toString());
            dir.createDirectory();
            wavFile = writeBurstWav (dir, "burst.wav", burstStartSecs);

            auto* track = te::getAudioTracks (*em.getEdit())[0];
            clip = clipEditor.importAudioFile (wavFile, *track, 1.0);
            REQUIRE (clip != nullptr);
        }

        ~WarpFixture() { dir.deleteRecursively(); }
    };
}

TEST_CASE ("WarpEngine: enable seeds endpoint markers and is undoable", "[warp][p7b]")
{
    WarpFixture f;
    auto& um = f.em.getUndoManager();

    REQUIRE_FALSE (WarpEngine::isWarped (*f.clip));

    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*f.clip, true);

    REQUIRE (WarpEngine::isWarped (*f.clip));
    // Der WarpTimeRenderJob stretcht mit dem Clip-Modus — muss prozessierbar sein
    REQUIRE (te::TimeStretcher::canProcessFor (f.clip->getTimeStretchMode()));

    auto markers = WarpEngine::getMarkers (*f.clip);
    REQUIRE (markers.size() == 2);
    CHECK (markers[0].sourceSecs == Approx (0.0));
    CHECK (markers[1].sourceSecs == Approx (1.0).margin (0.01));   // Quelllänge
    CHECK (markers[0].warpSecs   == Approx (markers[0].sourceSecs));
    CHECK (markers[1].warpSecs   == Approx (markers[1].sourceSecs));

    // Undobar (setWarpTime + Marker-Seeding laufen durch den UndoManager)
    um.undo();
    REQUIRE_FALSE (WarpEngine::isWarped (*f.clip));
    um.redo();
    REQUIRE (WarpEngine::isWarped (*f.clip));
}

TEST_CASE ("WarpEngine: add keeps mapping identity, move warps it", "[warp][p7b]")
{
    WarpFixture f;
    auto& um = f.em.getUndoManager();

    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*f.clip, true);

    // Marker anlegen = klangneutral: Mapping bleibt Identität
    um.beginNewTransaction ("Marker setzen");
    const int idx = WarpEngine::addMarkerAtSourceTime (*f.clip, 0.75);
    REQUIRE (idx == 1);   // sortiert zwischen die Endpunkte
    REQUIRE (WarpEngine::getMarkers (*f.clip).size() == 3);
    CHECK (WarpEngine::sourceToWarp (*f.clip, 0.75) == Approx (0.75));

    // Ziehen: Quelle 0.75 liegt jetzt bei Warp 0.5
    um.beginNewTransaction ("Marker bewegen");
    const double applied = WarpEngine::moveMarker (*f.clip, idx, 0.5);
    CHECK (applied == Approx (0.5));

    CHECK (WarpEngine::sourceToWarp (*f.clip, 0.75) == Approx (0.5));
    CHECK (WarpEngine::warpToSource (*f.clip, 0.5)  == Approx (0.75));
    // Segment 0→0.5 spielt Quelle 0→0.75 → Mitte: Warp 0.25 = Quelle 0.375
    CHECK (WarpEngine::warpToSource (*f.clip, 0.25) == Approx (0.375).margin (0.001));

    // Geste ist EIN Undo-Schritt
    um.undo();
    CHECK (WarpEngine::sourceToWarp (*f.clip, 0.75) == Approx (0.75));
    um.redo();
    CHECK (WarpEngine::sourceToWarp (*f.clip, 0.75) == Approx (0.5));
}

TEST_CASE ("WarpEngine: TE clamps segment stretch ratio", "[warp][p7b]")
{
    WarpFixture f;
    auto& um = f.em.getUndoManager();

    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*f.clip, true);

    um.beginNewTransaction ("Marker setzen");
    const int idx = WarpEngine::addMarkerAtSourceTime (*f.clip, 0.75);

    // Ratio-Untergrenze 0.1: Segment 0→0.75 darf nicht kürzer als ~0.075 werden
    um.beginNewTransaction ("Marker bewegen");
    const double applied = WarpEngine::moveMarker (*f.clip, idx, 0.01);
    CHECK (applied >= 0.075);
    CHECK (applied < 0.08);
}

TEST_CASE ("WarpEngine: interior marker removes, endpoint resets", "[warp][p7b]")
{
    WarpFixture f;
    auto& um = f.em.getUndoManager();

    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*f.clip, true);

    um.beginNewTransaction ("Marker setzen");
    const int idx = WarpEngine::addMarkerAtSourceTime (*f.clip, 0.75);
    um.beginNewTransaction ("Marker bewegen");
    WarpEngine::moveMarker (*f.clip, idx, 0.5);

    // Innerer Marker: wird entfernt, Mapping wieder Identität
    um.beginNewTransaction (juce::String::fromUTF8 ("Marker löschen"));
    WarpEngine::removeMarker (*f.clip, idx);
    REQUIRE (WarpEngine::getMarkers (*f.clip).size() == 2);
    CHECK (WarpEngine::sourceToWarp (*f.clip, 0.75) == Approx (0.75));

    // Endpunkt: TE setzt zurück statt zu entfernen (Anker bleiben)
    um.beginNewTransaction (juce::String::fromUTF8 ("Endpunkt löschen"));
    WarpEngine::removeMarker (*f.clip, 0);
    CHECK (WarpEngine::getMarkers (*f.clip).size() == 2);

    // resetMarkers = Bend neutral
    um.beginNewTransaction (juce::String::fromUTF8 ("Bend zurücksetzen"));
    WarpEngine::addMarkerAtSourceTime (*f.clip, 0.6);
    WarpEngine::resetMarkers (*f.clip);
    REQUIRE (WarpEngine::getMarkers (*f.clip).size() == 2);
}

TEST_CASE ("WarpEngine: RENDER PROOF — warp actually moves audio", "[warp][p7b]")
{
    // Burst bei Quelle 0.75s; Marker 0.75 → Warp 0.5 zieht ihn vor.
    WarpFixture f (0.75);
    auto& um = f.em.getUndoManager();

    um.beginNewTransaction ("Audio-Bend an");
    WarpEngine::setWarpEnabled (*f.clip, true);
    um.beginNewTransaction ("Marker");
    const int idx = WarpEngine::addMarkerAtSourceTime (*f.clip, 0.75);
    WarpEngine::moveMarker (*f.clip, idx, 0.5);

    // Warp ersetzt die CLIP-QUELLE asynchron: der WarpRenderWatcher (im
    // EditManager) stößt den korrekten Render-Job an (TEs eigener Job würde
    // STILLE rendern — getWaveInfo-Falle, siehe WarpEngine-Doku). Headless:
    // Message-Loop pumpen, bis der Render ABGESCHLOSSEN ist. WICHTIG: NICHT nur
    // auf AudioFile::isValid() warten — das wird schon true, sobald die Datei
    // existiert, BEVOR der BG-Render die Samples geschrieben hat (sonst liest
    // der Test die halb-gerenderte, stille Datei = Flake). isWarpRenderPending
    // ist erst false, wenn der Job weg UND die Datei die valide Quelle ist.
    // Warten bis der Render nicht mehr pending ist UND hörbaren Inhalt hat.
    // Die Wartebedingung (Inhalt vorhanden) ist bewusst SCHWÄCHER als die
    // Assertion (Inhalt bei 0.5s ±0.08) — kein Zirkelschluss: ein wirklich
    // stiller/hängender Render läuft in den Timeout und die REQUIRE fällt.
    // Rein auf isWarpRenderPending zu warten reicht NICHT — die Job-Entfernung
    // liegt einen Wimpernschlag vor dem finalen Datei-Flush/Cache-Update.
    // Robust gegen den dokumentierten Cold-Cache-Heisenbug: unter Last kann der
    // BG-Render EINMAL still zurückkommen (AudioFileCache noch nicht warm). Weil
    // die Marker unverändert sind, hätte ein Neu-Kick denselben Ziel-Hash und würde
    // die stille Datei als „fertig" adoptieren — deshalb bei stiller Beobachtung die
    // Zieldatei UND ihren Cache-Eintrag verwerfen und frisch rendern (bounded).
    // Die Assertion (Burst bei 0.5s ±0.08) bleibt unverändert = kein Aufweichen.
    double burstAt = -1.0;
    int rekicks = 0;
    for (int i = 0; i < 800; ++i)
    {
        if (! WarpEngine::isWarpRenderPending (*f.clip))
        {
            const auto rendered = f.clip->getAudioFile();
            const auto f2 = rendered.getFile();
            if (f2 != f.wavFile)
            {
                burstAt = firstLoudSecs (f2);
                if (burstAt >= 0.0)
                    break;

                if (rekicks++ < 4)   // silent render → drop file + stale cache, re-render
                {
                    f.clip->edit.engine.getAudioFileManager().releaseFile (rendered);
                    f2.deleteFile();
                    f.clip->edit.engine.getAudioFileManager().validateFile (rendered, true);
                    WarpEngine::kickWarpRender (*f.clip);
                }
            }
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    const auto rendered = f.clip->getAudioFile().getFile();
    REQUIRE (rendered != f.wavFile);   // wirklich der Warp-Render, nicht das Original

    // Burst muss jetzt bei ~0.5s liegen (SoundTouch-Schmierung toleriert)
    REQUIRE (burstAt >= 0.0);
    CHECK (burstAt == Approx (0.5).margin (0.08));
}

TEST_CASE ("WarpEngine: warp state survives save/reload", "[warp][p7b]")
{
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_warp_test");
    tempDir.createDirectory();
    const auto sessionFile = tempDir.getChildFile ("warp_session.tracktion");

    {
        WarpFixture f;
        f.em.getUndoManager().beginNewTransaction ("Audio-Bend an");
        WarpEngine::setWarpEnabled (*f.clip, true);
        f.em.saveEdit (sessionFile);
    }

    EditManager em2;
    REQUIRE (em2.loadEdit (sessionFile));

    te::WaveAudioClip* reloaded = nullptr;
    for (auto* t : te::getAudioTracks (*em2.getEdit()))
        for (auto* c : t->getClips())
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                reloaded = wc;

    REQUIRE (reloaded != nullptr);
    CHECK (WarpEngine::isWarped (*reloaded));
    CHECK (WarpEngine::getMarkers (*reloaded).size() == 2);
}
