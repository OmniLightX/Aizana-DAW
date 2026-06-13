#include "ReferenceAnalyzer.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>
#include <ebur128.h>
#include <thread>

namespace
{
    /** Load any readable file as stereo float at its native rate. */
    bool loadStereo (const juce::File& file, juce::AudioBuffer<float>& out, double& sampleRate)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            return false;

        const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                       (juce::int64) (reader->sampleRate * 600.0));
        juce::AudioBuffer<float> src (juce::jmin (2, (int) reader->numChannels), len);
        reader->read (&src, 0, len, 0, true, true);

        out.setSize (2, len);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, src, juce::jmin (ch, src.getNumChannels() - 1), 0, len);

        sampleRate = reader->sampleRate;
        return true;
    }

    /** Integrated EBU R128 loudness of an interleaved-on-demand stereo buffer. */
    double measureIntegratedLufs (const juce::AudioBuffer<float>& stereo, double sampleRate)
    {
        ebur128_state* state = ebur128_init (2, (unsigned long) sampleRate, EBUR128_MODE_I);
        if (state == nullptr)
            return -100.0;

        constexpr int kBlock = 4096;
        std::vector<float> interleaved ((size_t) kBlock * 2);
        const int n = stereo.getNumSamples();

        for (int start = 0; start < n; start += kBlock)
        {
            const int len = juce::jmin (kBlock, n - start);
            for (int i = 0; i < len; ++i)
            {
                interleaved[(size_t) i * 2]     = stereo.getSample (0, start + i);
                interleaved[(size_t) i * 2 + 1] = stereo.getSample (1, start + i);
            }
            ebur128_add_frames_float (state, interleaved.data(), (size_t) len);
        }

        double lufs = -100.0;
        ebur128_loudness_global (state, &lufs);
        ebur128_destroy (&state);
        return lufs;
    }
}

//==============================================================================
void ReferenceAnalyzer::measureBandBalance (const float* mono, int numSamples,
                                            double sampleRate,
                                            double& lowDb, double& midDb, double& highDb)
{
    lowDb = midDb = highDb = 0.0;
    if (mono == nullptr || numSamples < 8192 || sampleRate <= 0.0)
        return;

    constexpr int fftOrder = 12;
    constexpr int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
        juce::dsp::WindowingFunction<float>::hann);

    double lowSum = 0.0, midSum = 0.0, highSum = 0.0;
    std::vector<float> buf ((size_t) fftSize * 2);

    const int numWindows = juce::jlimit (1, 24, numSamples / fftSize);
    const int stride     = juce::jmax (fftSize, numSamples / numWindows);

    for (int w = 0; w < numWindows; ++w)
    {
        const int start = w * stride;
        if (start + fftSize > numSamples)
            break;

        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (mono + start, mono + start + fftSize, buf.begin());
        window.multiplyWithWindowingTable (buf.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (buf.data());

        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const double freq   = bin * sampleRate / fftSize;
            const double energy = (double) buf[(size_t) bin] * (double) buf[(size_t) bin];
            if      (freq < 250.0)   lowSum  += energy;
            else if (freq < 4000.0)  midSum  += energy;
            else if (freq < 20000.0) highSum += energy;
        }
    }

    const auto toDb = [] (double e) { return 10.0 * std::log10 (juce::jmax (1.0e-12, e)); };
    const double l = toDb (lowSum), m = toDb (midSum), h = toDb (highSum);
    const double mean = (l + m + h) / 3.0;

    // Relative emphasis around 0 — clamped: these nudge gain staging, they
    // must never encode "this reference is 18 dB bassier" literally.
    lowDb  = juce::jlimit (-3.0, 3.0, (l - mean) * 0.25);
    midDb  = juce::jlimit (-3.0, 3.0, (m - mean) * 0.25);
    highDb = juce::jlimit (-3.0, 3.0, (h - mean) * 0.25);
}

//==============================================================================
double ReferenceAnalyzer::measureVocalPresence (const float* mono, int numSamples,
                                                double sampleRate)
{
    if (mono == nullptr || numSamples < 8192 || sampleRate <= 0.0)
        return 0.0;

    constexpr int fftOrder = 12;
    constexpr int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
        juce::dsp::WindowingFunction<float>::hann);

    double presenceSum = 0.0, restSum = 0.0;
    std::vector<float> buf ((size_t) fftSize * 2);

    const int numWindows = juce::jlimit (1, 24, numSamples / fftSize);
    const int stride     = juce::jmax (fftSize, numSamples / numWindows);

    for (int w = 0; w < numWindows; ++w)
    {
        const int start = w * stride;
        if (start + fftSize > numSamples)
            break;

        std::fill (buf.begin(), buf.end(), 0.0f);
        std::copy (mono + start, mono + start + fftSize, buf.begin());
        window.multiplyWithWindowingTable (buf.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (buf.data());

        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const double freq   = bin * sampleRate / fftSize;
            const double energy = (double) buf[(size_t) bin] * (double) buf[(size_t) bin];
            if (freq >= 1000.0 && freq < 4000.0) presenceSum += energy;
            else if (freq < 16000.0)             restSum     += energy;
        }
    }

    const auto toDb = [] (double e) { return 10.0 * std::log10 (juce::jmax (1.0e-12, e)); };
    // restSum spans ~3x the bandwidth — compare against its per-band share
    return juce::jlimit (-3.0, 3.0, (toDb (presenceSum) - toDb (restSum / 3.0)) * 0.25);
}

//==============================================================================
ReferenceProfile ReferenceAnalyzer::analyzeFile (const juce::File& file)
{
    ReferenceProfile profile;

    juce::AudioBuffer<float> stereo;
    double sampleRate = 0.0;
    if (! loadStereo (file, stereo, sampleRate))
        return profile;   // invalid (name empty)

    profile.name               = file.getFileNameWithoutExtension();
    profile.fromReferenceTrack = true;
    profile.targetLufs         = measureIntegratedLufs (stereo, sampleRate);

    // Band balance from a mono mixdown
    juce::AudioBuffer<float> mono (1, stereo.getNumSamples());
    mono.clear();
    mono.addFrom (0, 0, stereo, 0, 0, stereo.getNumSamples(), 0.5f);
    mono.addFrom (0, 0, stereo, 1, 0, stereo.getNumSamples(), 0.5f);

    measureBandBalance (mono.getReadPointer (0), mono.getNumSamples(), sampleRate,
                        profile.lowDb, profile.midDb, profile.highDb);
    profile.vocalDb = measureVocalPresence (mono.getReadPointer (0),
                                            mono.getNumSamples(), sampleRate);

    return profile;
}

//==============================================================================
void ReferenceAnalyzer::analyzeAsync (const juce::File& file,
                                      std::function<void (ReferenceProfile)> onDone)
{
    std::thread ([file, onDone = std::move (onDone)]()
    {
        auto profile = analyzeFile (file);
        juce::MessageManager::callAsync ([onDone, profile]()
        {
            if (onDone)
                onDone (profile);
        });
    }).detach();
}
