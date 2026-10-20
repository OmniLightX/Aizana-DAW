#include "Mp3Encoder.h"
#include <lame.h>
#include <array>

juce::String Mp3Encoder::encode(const juce::File& pcm, const juce::File& mp3,
                               int bitrateKbps, const std::function<bool()>& cancelled)
{
    juce::WavAudioFormat wav;
    auto input = pcm.createInputStream();
    if (!input) return "MP3: PCM-Datei konnte nicht geoeffnet werden.";
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(input.release(), true));
    if (!reader || reader->numChannels != 2
        || (reader->sampleRate != 44100 && reader->sampleRate != 48000))
        return "MP3: Ungueltige PCM-Zwischendatei.";
    if (bitrateKbps != 128 && bitrateKbps != 192 && bitrateKbps != 256 && bitrateKbps != 320)
        return "MP3: Ungueltige Bitrate.";

    std::unique_ptr<lame_global_flags, decltype(&lame_close)> encoder(lame_init(), lame_close);
    if (!encoder)
        return "MP3: Encoder konnte nicht gestartet werden.";
    auto* g = encoder.get();
    lame_set_in_samplerate(g, static_cast<int>(reader->sampleRate));
    lame_set_out_samplerate(g, static_cast<int>(reader->sampleRate));
    lame_set_num_channels(g, 2);
    lame_set_num_samples(g, static_cast<unsigned long>(reader->lengthInSamples));
    lame_set_brate(g, bitrateKbps);
    lame_set_VBR(g, vbr_off);
    lame_set_quality(g, 2);
    lame_set_bWriteVbrTag(g, 1); // Contains encoder delay/padding for gapless playback.
    lame_set_write_id3tag_automatic(g, 0);
    if (lame_init_params(g) < 0)
        return "MP3: Encoder-Einstellungen konnten nicht geladen werden.";

    auto output = mp3.createOutputStream();
    if (!output || output->failedToOpen())
        return "MP3: Zieldatei kann nicht geschrieben werden.";
    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer(2, blockSize);
    std::array<unsigned char, blockSize * 2 + 7200> encoded {};
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize)
    {
        if (cancelled && cancelled()) return "Export abgebrochen.";
        const int count = static_cast<int>(std::min<juce::int64>(blockSize, reader->lengthInSamples - position));
        if (!reader->read(&buffer, 0, count, position, true, true))
            return "MP3: PCM-Daten konnten nicht gelesen werden.";
        const int bytes = lame_encode_buffer_ieee_float(g, buffer.getReadPointer(0),
            buffer.getReadPointer(1), count, encoded.data(), static_cast<int>(encoded.size()));
        if (bytes < 0 || !output->write(encoded.data(), static_cast<size_t>(bytes)))
            return "MP3: Kodierung oder Schreiben fehlgeschlagen.";
    }
    if (cancelled && cancelled()) return "Export abgebrochen.";
    const int bytes = lame_encode_flush(g, encoded.data(), static_cast<int>(encoded.size()));
    if (bytes < 0 || !output->write(encoded.data(), static_cast<size_t>(bytes)))
        return "MP3: Abschluss der Kodierung fehlgeschlagen.";
    const auto tagBytes = lame_get_lametag_frame(g, encoded.data(), encoded.size());
    if (tagBytes == 0 || tagBytes > encoded.size() || !output->setPosition(0)
        || !output->write(encoded.data(), tagBytes))
        return "MP3: Laengeninformationen konnten nicht geschrieben werden.";
    output->flush();
    if (output->getStatus().failed()) return "MP3: Schreibfehler beim Speichern.";
    return {};
}
