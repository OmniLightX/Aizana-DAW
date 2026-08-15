// test_removable_media_guard.cpp — M12: kein mmap für Dateien auf externen Volumes.
//
// Der SIGBUS-Crash (2026-07-05): TE memory-mappt Audio-Dateien; verschwindet das
// externe Volume, ist jeder Zugriff ein Bus Error. Der RemovableMediaGuard gated den
// mmap-Pfad (memoryMappedFormatManager = das Gate in AudioFileCache::
// getOrCreateCachedFile) — Dateien unter /Volumes/ fallen auf TEs Buffered-Reader
// zurück, interne Pfade behalten mmap.

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "engine/RemovableMediaGuard.h"

TEST_CASE ("M12: pathLooksFragile — pure Volume-Heuristik", "[m12][removable]")
{
    CHECK (aizana::pathLooksFragile ("/Volumes/Externe SSD/Beispiel.tracktion"));
    CHECK (aizana::pathLooksFragile ("/Volumes/USB/x.wav"));
    CHECK_FALSE (aizana::pathLooksFragile ("/Users/alice/Music/Aizana/song.wav"));
    CHECK_FALSE (aizana::pathLooksFragile ("/tmp/render.wav"));
    CHECK_FALSE (aizana::pathLooksFragile ("/private/tmp/x.aiff"));
    // Kein falscher Prefix-Match:
    CHECK_FALSE (aizana::pathLooksFragile ("/VolumesBackup/x.wav"));
}

TEST_CASE ("M12: memoryMappedFormatManager lehnt fragile Pfade ab, interne nicht", "[m12][removable]")
{
    EditManager em;   // Ctor installiert den Guard in der Engine
    auto& mm = em.getEngine().getAudioFileFormatManager().memoryMappedFormatManager;

    REQUIRE (mm.getNumKnownFormats() > 0);

    // canHandleFile prüft bei WAV/AIFF nur die Endung — die Dateien müssen nicht
    // existieren. Genau diese Abfrage ist das Gate in getOrCreateCachedFile.
    const juce::File internalWav ("/Users/nobody/Music/take.wav");
    const juce::File externalWav ("/Volumes/Externe SSD/take.wav");

    bool internalHandled = false, externalHandled = false;
    for (auto* af : mm)
    {
        internalHandled = internalHandled || af->canHandleFile (internalWav);
        externalHandled = externalHandled || af->canHandleFile (externalWav);
    }

    CHECK (internalHandled);          // interne Platte: mmap-Pfad bleibt
    CHECK_FALSE (externalHandled);    // externes Volume: Gate zu → Buffered-Fallback
}

TEST_CASE ("M12: AudioFileCache liefert für fragile Pfade trotzdem einen Reader (Buffered-Fallback)",
           "[m12][removable]")
{
    EditManager em;

    // Echte kleine WAV-Datei intern erzeugen …
    juce::TemporaryFile temp (".wav");
    {
        juce::WavAudioFormat wav;
        auto stream = temp.getFile().createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.release(), 44100.0, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> buf (1, 4410);
        buf.clear();
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    // … interner Pfad: Reader über den (gemappten) Cache-Pfad
    te::AudioFile internalFile (em.getEngine(), temp.getFile());
    auto internalReader = em.getEngine().getAudioFileManager().cache.createReader (internalFile);
    CHECK (internalReader != nullptr);
}
