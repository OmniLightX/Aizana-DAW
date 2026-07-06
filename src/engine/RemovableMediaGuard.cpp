#include "RemovableMediaGuard.h"

namespace aizana
{
    bool pathLooksFragile (const juce::String& absolutePath) noexcept
    {
        // macOS: externe Platten, USB-Sticks, DMGs und Netz-Mounts liegen unter
        // /Volumes/. Die Boot-Platte ist "/" — normale interne Pfade (/Users/…)
        // matchen nicht. (Der seltene explizite /Volumes/Macintosh\ HD-Pfad wird
        // mit-gegated: Buffered-I/O ist dort nur ein Perf-, kein Korrektheitsthema.)
        return absolutePath.startsWith ("/Volumes/");
    }

    bool isOnFragileVolume (const juce::File& file)
    {
        if (pathLooksFragile (file.getFullPathName()))
            return true;

        // Gurt für exotische Mount-Punkte. JUCE fragt das Filesystem — nur als
        // Zweitcheck, der String-Check oben entscheidet den Normalfall allein.
        return file.exists() && file.isOnRemovableDrive();
    }

    namespace
    {
        /**
         * Delegiert alles an das echte Format, lehnt aber canHandleFile() für
         * Dateien auf fragilen Volumes ab. Nur im memoryMappedFormatManager
         * registriert — der wirkt ausschließlich als GATE für den mmap-Pfad
         * (AudioFileCache::getOrCreateCachedFile); Lesen/Schreiben läuft weiter
         * über read-/writeFormatManager mit den Original-Formaten.
         */
        class FragileVolumeGuardFormat final : public juce::AudioFormat
        {
        public:
            explicit FragileVolumeGuardFormat (std::unique_ptr<juce::AudioFormat> innerFormat)
                : juce::AudioFormat (innerFormat->getFormatName(), innerFormat->getFileExtensions()),
                  inner (std::move (innerFormat))
            {
            }

            bool canHandleFile (const juce::File& f) override
            {
                return ! isOnFragileVolume (f) && inner->canHandleFile (f);
            }

            juce::Array<int> getPossibleSampleRates() override   { return inner->getPossibleSampleRates(); }
            juce::Array<int> getPossibleBitDepths() override     { return inner->getPossibleBitDepths(); }
            bool canDoStereo() override                          { return inner->canDoStereo(); }
            bool canDoMono() override                            { return inner->canDoMono(); }
            bool isCompressed() override                         { return inner->isCompressed(); }
            juce::StringArray getQualityOptions() override       { return inner->getQualityOptions(); }

            juce::AudioFormatReader* createReaderFor (juce::InputStream* in, bool deleteStream) override
            {
                return inner->createReaderFor (in, deleteStream);
            }

            juce::MemoryMappedAudioFormatReader* createMemoryMappedReader (const juce::File& f) override
            {
                return isOnFragileVolume (f) ? nullptr : inner->createMemoryMappedReader (f);
            }

            juce::MemoryMappedAudioFormatReader* createMemoryMappedReader (juce::FileInputStream* fin) override
            {
                if (fin != nullptr && isOnFragileVolume (fin->getFile()))
                {
                    delete fin;   // Vertrag wie JUCE-Formate: Reader übernimmt den Stream
                    return nullptr;
                }
                return inner->createMemoryMappedReader (fin);
            }

            juce::AudioFormatWriter* createWriterFor (juce::OutputStream* out, double sampleRate,
                                                      unsigned int numChannels, int bitsPerSample,
                                                      const juce::StringPairArray& metadata,
                                                      int qualityOptionIndex) override
            {
                return inner->createWriterFor (out, sampleRate, numChannels, bitsPerSample,
                                               metadata, qualityOptionIndex);
            }

        private:
            std::unique_ptr<juce::AudioFormat> inner;
        };
    }

    void installRemovableMediaGuard (te::Engine& engine)
    {
        auto& mm = engine.getAudioFileFormatManager().memoryMappedFormatManager;
        mm.clearFormats();

        // Spiegel der TE-Registrierung (tracktion_AudioFormatManager.cpp):
        // Wav (Default), Aiff, FloatAudioFormat (Frozen-Files).
        mm.registerFormat (new FragileVolumeGuardFormat (std::make_unique<juce::WavAudioFormat>()), true);
        mm.registerFormat (new FragileVolumeGuardFormat (std::make_unique<juce::AiffAudioFormat>()), false);
        mm.registerFormat (new FragileVolumeGuardFormat (std::make_unique<te::FloatAudioFormat>()), false);
    }
}
