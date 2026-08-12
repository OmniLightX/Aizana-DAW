#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include <memory>

/**
 * AudioThumbnailPool — EIN AudioFormatManager, EIN AudioThumbnailCache und EIN
 * AudioThumbnail PRO QUELLDATEI fuer die ganze App (via juce::SharedResourcePointer).
 *
 * Warum der geteilte Cache allein nicht reicht:
 * Er speichert FERTIGE Thumbnails, keine halben. ClipComponents werden bei jedem
 * rebuildTrackRows neu gebaut — beim Import einer einzigen Datei gemessen FUENFMAL
 * (GUI-Testlauf 2026-08-12). Legte jede neue Component ihr eigenes AudioThumbnail an,
 * begann der Lesevorgang jedes Mal bei null. Bei langen Dateien kann er so nie fertig
 * werden, der Cache bekommt nie einen Eintrag, und die Wellenform erscheint spaet
 * oder gar nicht.
 *
 * Deshalb liegt das AudioThumbnail hier: eine Instanz je Quelldatei, die
 * Component-Neubauten ueberlebt. Ein angefangener Lesevorgang laeuft weiter.
 *
 * NUR VOM MESSAGE THREAD benutzen (UI-Konstruktion und paint) — die Map ist nicht
 * gegen gleichzeitigen Zugriff geschuetzt.
 *
 * Usage:  juce::SharedResourcePointer<AudioThumbnailPool> pool;
 *         auto& thumb = pool->forFile (audioFile);
 */
struct AudioThumbnailPool
{
    AudioThumbnailPool()
    {
        formatManager.registerBasicFormats();
    }

    /** Thumbnail für diese Datei — legt es beim ersten Aufruf an und behält es. */
    juce::AudioThumbnail& forFile (const juce::File& file)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const auto key = file.getFullPathName();

        if (auto it = thumbs.find (key); it != thumbs.end())
            return *it->second;

        // Obergrenze wie beim Cache: eine Session mit mehr als 512 verschiedenen
        // Quelldateien darf nicht unbegrenzt Speicher binden. Ein Thumbnail kostet
        // wenige zehn KB, ein Leeren ist also selten und billig.
        if (thumbs.size() >= 512)
            thumbs.clear();

        auto thumb = std::make_unique<juce::AudioThumbnail> (512, formatManager, cache);
        thumb->setSource (new juce::FileInputSource (file));

        auto& ref = *thumb;
        thumbs.emplace (key, std::move (thumb));
        return ref;
    }

    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache cache { 512 };   // plenty for large sessions

private:
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs;
};
