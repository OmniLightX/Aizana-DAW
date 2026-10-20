#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace Mp3Encoder
{
// Worker-thread operation. Input must be stereo PCM at 44.1 or 48 kHz.
// The caller owns temporary-file cleanup and publication of the completed file.
juce::String encode(const juce::File& pcm, const juce::File& mp3, int bitrateKbps,
                    const std::function<bool()>& cancelled);
}
