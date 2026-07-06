#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * RemovableMediaGuard (M12) — kein SIGBUS mehr durch verschwundene externe Platten.
 *
 * Problem: TE memory-mappt Audio-Dateien (AudioFileCache/CachedFile). Verschwindet
 * das Volume (SSD abgezogen, Netz-Mount weg), werden die gemappten Seiten ungültig —
 * JEDER Zugriff (RefresherThread::touchSample ODER der Audio-Thread beim Lesen) ist
 * dann ein Bus Error, in-process nicht abfangbar. Realer Crash 2026-07-05
 * (`Aizana-2026-07-05-194553.ips`, Thread "CacheRefresher").
 *
 * Fix ohne TE-Patch: `AudioFileCache::getOrCreateCachedFile` gated den mmap-Pfad
 * über den PUBLIC `AudioFileFormatManager::memoryMappedFormatManager`. Wir ersetzen
 * dessen Formate durch Wrapper, deren canHandleFile() Dateien auf "fragilen" Volumes
 * (alles unter /Volumes/ = extern/Wechselmedium/Netz auf macOS) ABLEHNT. TE fällt
 * für diese Dateien automatisch auf seinen BufferingAudioReader-Pfad zurück:
 * normales read() auf einem Background-TimeSliceThread — ein gezogenes Volume
 * liefert dann Lesefehler/Stille statt SIGBUS.
 *
 * Nebeneffekt (M13-Verwandtschaft): mmap-Page-Faults schlugen bisher auf dem
 * AUDIO-THREAD auf, wenn das USB-Volume Latenz-Spikes hatte (I-01-Verletzung durch
 * die Hintertür). Der Buffered-Pfad prefetcht ~5 s auf dem Background-Thread —
 * externe Sessions streamen damit auch dropout-ärmer.
 *
 * Interne Platte (der schnelle Normalfall) bleibt unverändert auf mmap.
 */
namespace aizana
{
    /** Pure Heuristik (golden-testbar): liegt der absolute Pfad auf einem Volume,
        das unangekündigt verschwinden kann? macOS mountet alles Externe/Netzwerk
        unter /Volumes/ — die Boot-Platte ist "/" und taucht dort nicht auf. */
    bool pathLooksFragile (const juce::String& absolutePath) noexcept;

    /** Wie oben, plus Laufzeit-Check juce::File::isOnRemovableDrive() als Gurt. */
    bool isOnFragileVolume (const juce::File& file);

    /** In der Engine installieren (einmal, nach Konstruktion): ersetzt die Formate
        des memoryMappedFormatManager durch Guard-Wrapper. */
    void installRemovableMediaGuard (te::Engine& engine);
}
