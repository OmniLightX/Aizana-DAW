#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <complex>
#include <functional>
#include <vector>

/** Worker-only spectral contract matching Mega 53: periodic Hann, FFT 2048,
    hop 512, centred reflection padding, no FFT normalization on forward. */
struct StemSpectralTransform
{
    struct Spectrum
    {
        int samples = 0, frames = 0;
        // [frame][positive-frequency bin 0..1024][stereo channel]
        std::vector<std::complex<float>> bins;
    };
    static Spectrum encode (const juce::AudioBuffer<float>& stereo);
    static juce::AudioBuffer<float> decode (const Spectrum&);
};

/** Local CPU inference on a dedicated worker. Never call from the audio callback.
    Outputs: Drums, Bass, Other (residual), Vocals, Guitar, Piano, same length as
    the 44.1 kHz stereo input. Output is published only on complete success. */
struct RoFormerStemBackend
{
    static juce::Result separate (const juce::File& model,
                                  const juce::AudioBuffer<float>& stereo44k,
                                  int threads,
                                  std::function<void (float)> progress,
                                  std::function<bool()> shouldCancel,
                                  std::vector<juce::AudioBuffer<float>>& outputs);
};
