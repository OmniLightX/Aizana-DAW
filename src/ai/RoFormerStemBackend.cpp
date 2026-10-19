#include "RoFormerStemBackend.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <limits>
#if AIZANA_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace
{
    constexpr int fftSize = 2048, hop = 512, bins = 1025, width = bins * 4;
    constexpr int chunkSamples = 4 * 44100;
    std::array<float, fftSize> hann()
    {
        std::array<float, fftSize> result {};
        for (int i = 0; i < fftSize; ++i)
            result[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
        return result;
    }
    int reflect (int i, int length)
    {
        if (length <= 1) return 0;
        const auto period = (int64_t) 2 * (length - 1);
        auto p = (int64_t) i % period;
        if (p < 0) p += period;
        return (int) (p < length ? p : period - p);
    }
    size_t indexOf (int frame, int frequency, int channel)
    {
        return ((size_t) frame * bins + (size_t) frequency) * 2 + (size_t) channel;
    }
}

StemSpectralTransform::Spectrum StemSpectralTransform::encode (const juce::AudioBuffer<float>& audio)
{
    Spectrum result;
    const auto n = audio.getNumSamples();
    if (audio.getNumChannels() != 2 || n <= 0 || n > std::numeric_limits<int>::max() - 4096)
        return result;
    result.samples = n;
    result.frames = 1 + n / hop;
    result.bins.resize ((size_t) result.frames * bins * 2);
    const auto window = hann();
    juce::dsp::FFT fft (11);
    std::array<std::complex<float>, fftSize> in {}, out {};
    for (int f = 0; f < result.frames; ++f)
        for (int c = 0; c < 2; ++c)
        {
            const auto* source = audio.getReadPointer (c);
            for (int i = 0; i < fftSize; ++i)
                in[(size_t) i] = { source[reflect (f * hop + i - fftSize / 2, n)] * window[(size_t) i], 0.0f };
            fft.perform (in.data(), out.data(), false);
            for (int k = 0; k < bins; ++k)
                result.bins[indexOf (f, k, c)] = out[(size_t) k];
        }
    return result;
}

juce::AudioBuffer<float> StemSpectralTransform::decode (const Spectrum& spec)
{
    if (spec.samples <= 0 || spec.samples > std::numeric_limits<int>::max() - 4096
        || spec.frames != 1 + spec.samples / hop
        || spec.bins.size() != (size_t) spec.frames * bins * 2)
        return {};
    const int total = (spec.frames - 1) * hop + fftSize;
    juce::AudioBuffer<float> sum (2, total);
    sum.clear();
    std::vector<float> norm ((size_t) total, 0.0f);
    const auto window = hann();
    juce::dsp::FFT fft (11);
    std::array<std::complex<float>, fftSize> in {}, out {};
    for (int f = 0; f < spec.frames; ++f)
    {
        for (int i = 0; i < fftSize; ++i)
            norm[(size_t) (f * hop + i)] += window[(size_t) i] * window[(size_t) i];
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < bins; ++k)
                in[(size_t) k] = spec.bins[indexOf (f, k, c)];
            for (int k = bins; k < fftSize; ++k)
                in[(size_t) k] = std::conj (in[(size_t) (fftSize - k)]);
            fft.perform (in.data(), out.data(), true);
            auto* destination = sum.getWritePointer (c, f * hop);
            for (int i = 0; i < fftSize; ++i)
                destination[i] += out[(size_t) i].real() * window[(size_t) i];
        }
    }
    juce::AudioBuffer<float> result (2, spec.samples);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < spec.samples; ++i)
        {
            const auto at = i + fftSize / 2;
            result.setSample (c, i, norm[(size_t) at] > 1.0e-8f
                ? sum.getSample (c, at) / norm[(size_t) at] : 0.0f);
        }
    return result;
}

juce::Result RoFormerStemBackend::separate (
    const juce::File& file, const juce::AudioBuffer<float>& audio, int threads,
    std::function<void (float)> progress, std::function<bool()> shouldCancel,
    std::vector<juce::AudioBuffer<float>>& outputs)
{
    outputs.clear();
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    if (cancelled()) return juce::Result::fail ("Abgebrochen");
    if (! file.existsAsFile()) return juce::Result::fail ("Stem-Modell nicht installiert");
    const int n = audio.getNumSamples();
    if (n <= 0 || audio.getNumChannels() != 2)
        return juce::Result::fail ("Stem-Trennung erwartet Stereo-Audio mit 44,1 kHz");
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (audio.getSample (c, i)))
                return juce::Result::fail ("Audio enthaelt ungueltige Samples");
#if ! AIZANA_HAVE_ONNXRUNTIME
    juce::ignoreUnused (threads, progress);
    return juce::Result::fail ("Diese Fassung wurde ohne ONNX Runtime gebaut");
#else
    try
    {
        Ort::Env environment (ORT_LOGGING_LEVEL_WARNING, "aizana-stems");
        environment.DisableTelemetryEvents();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads (juce::jlimit (1, 16, threads));
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        Ort::Session session (environment, file.getFullPathName().toRawUTF8(), options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() != 1)
            return juce::Result::fail ("Unbekanntes Stem-Modellformat");
        const auto inputInfo = session.GetInputTypeInfo (0);
        const auto outputInfo = session.GetOutputTypeInfo (0);
        const auto inputTensor = inputInfo.GetTensorTypeAndShapeInfo();
        const auto outputTensor = outputInfo.GetTensorTypeAndShapeInfo();
        const auto outputShape = outputTensor.GetShape();
        if (inputTensor.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || outputTensor.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
            || inputTensor.GetShape() != std::vector<int64_t> { 1, -1, width }
            || outputShape.size() != 4
            || (outputShape[0] != 1 && outputShape[0] != -1)
            || outputShape[1] != 5 || outputShape[2] != -1 || outputShape[3] != width)
            return juce::Result::fail ("Stem-Modell hat ein unpassendes Tensorformat");
        std::vector<juce::AudioBuffer<float>> result;
        for (int i = 0; i < 6; ++i)
        {
            result.emplace_back (2, n);
            result.back().clear();
        }
        std::vector<float> norm ((size_t) n, 0.0f);
        constexpr int destination[] = { 0, 1, 3, 4, 5 };
        const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        juce::AudioBuffer<float> chunk (2, chunkSamples);
        // Fixed chunks bound working memory; crossfades remove inference edges.
        for (int64_t offset64 = 0; offset64 < n; offset64 += chunkSamples / 2)
        {
            if (cancelled()) return juce::Result::fail ("Abgebrochen");
            const int offset = (int) offset64, actual = std::min (chunkSamples, n - offset);
            chunk.clear();
            for (int c = 0; c < 2; ++c) chunk.copyFrom (c, 0, audio, c, offset, actual);
            const auto spectrum = StemSpectralTransform::encode (chunk);
            std::vector<float> tensor (spectrum.bins.size() * 2);
            for (size_t i = 0; i < spectrum.bins.size(); ++i)
            {
                tensor[i * 2] = spectrum.bins[i].real();
                tensor[i * 2 + 1] = spectrum.bins[i].imag();
            }
            const std::array<int64_t, 3> shape { 1, spectrum.frames, width };
            auto input = Ort::Value::CreateTensor<float> (memory, tensor.data(), tensor.size(), shape.data(), shape.size());
            const char* inputNames[] = { "spectrum" };
            const char* outputNames[] = { "masks" };
            auto masks = session.Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 1);
            const auto info = masks.at (0).GetTensorTypeAndShapeInfo();
            if (info.GetShape() != std::vector<int64_t> { 1, 5, spectrum.frames, width })
                return juce::Result::fail ("Stem-Modell lieferte eine unpassende Ausgabe");
            if (cancelled()) return juce::Result::fail ("Abgebrochen");
            const auto* values = masks[0].GetTensorData<float>();
            const auto stride = spectrum.bins.size() * 2;
            for (int stem = 0; stem < 5; ++stem)
            {
                auto estimated = spectrum;
                for (size_t i = 0; i < estimated.bins.size(); ++i)
                {
                    const auto re = values[(size_t) stem * stride + i * 2];
                    const auto im = values[(size_t) stem * stride + i * 2 + 1];
                    if (! std::isfinite (re) || ! std::isfinite (im))
                        return juce::Result::fail ("Stem-Modell lieferte ungueltige Samples");
                    // Match upstream zero_dc: frequency zero in both channels.
                    estimated.bins[i] = i % (bins * 2) < 2 ? std::complex<float> {}
                        : spectrum.bins[i] * std::complex<float> { re, im };
                }
                const auto waveform = StemSpectralTransform::decode (estimated);
                for (int i = 0; i < actual; ++i)
                {
                    const float weight = (float) std::min (i + 1, chunkSamples - i) / (chunkSamples / 2);
                    for (int c = 0; c < 2; ++c)
                        result[(size_t) destination[stem]].addSample (c, offset + i, waveform.getSample (c, i) * weight);
                }
            }
            for (int i = 0; i < actual; ++i)
                norm[(size_t) offset + i] += (float) std::min (i + 1, chunkSamples - i) / (chunkSamples / 2);
            if (progress) progress ((float) std::min<int64_t> (n, offset64 + chunkSamples / 2) / n);
        }
        if (cancelled()) return juce::Result::fail ("Abgebrochen");
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float other = audio.getSample (c, i);
                for (int stem : destination)
                {
                    const float value = result[(size_t) stem].getSample (c, i) / norm[(size_t) i];
                    if (! std::isfinite (value)) return juce::Result::fail ("Stem-Ausgabe ist ungueltig");
                    result[(size_t) stem].setSample (c, i, value);
                    other -= value;
                }
                if (! std::isfinite (other)) return juce::Result::fail ("Stem-Rest ist ungueltig");
                result[2].setSample (c, i, other);
            }
        outputs = std::move (result);
        if (progress) progress (1.0f);
        return juce::Result::ok();
    }
    catch (const std::exception& e)
    {
        return juce::Result::fail ("Stem-Modell konnte nicht verarbeitet werden: " + juce::String (e.what()));
    }
#endif
}
