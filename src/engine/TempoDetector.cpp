#include "TempoDetector.h"

#include <thread>
#include <vector>

namespace
{
    constexpr int    kHop          = 512;     // onset envelope frame hop (samples)
    constexpr double kMaxAnalysisS = 60.0;    // analyse at most the first minute
    constexpr double kMinBpm       = 60.0;
    constexpr double kMaxBpm       = 200.0;
}

//==============================================================================
double TempoDetector::detectBpm (const juce::AudioBuffer<float>& mono, double sampleRate)
{
    if (sampleRate <= 0.0 || mono.getNumSamples() < kHop * 8)
        return 0.0;

    const float* in = mono.getReadPointer (0);
    const int numFrames = mono.getNumSamples() / kHop;
    if (numFrames < 16)
        return 0.0;

    // Onset-strength envelope: rectified frame-energy difference
    std::vector<double> flux ((size_t) numFrames, 0.0);
    double prevEnergy = 0.0;
    for (int f = 0; f < numFrames; ++f)
    {
        double e = 0.0;
        const float* frame = in + (size_t) f * kHop;
        for (int i = 0; i < kHop; ++i)
            e += (double) frame[i] * frame[i];
        flux[(size_t) f] = juce::jmax (0.0, e - prevEnergy);
        prevEnergy = e;
    }

    // Remove DC of the envelope so autocorrelation peaks on periodicity
    double mean = 0.0;
    for (double v : flux) mean += v;
    mean /= (double) flux.size();
    for (double& v : flux) v -= mean;

    // Autocorrelation over the lag range corresponding to kMinBpm..kMaxBpm
    const double framesPerSecond = sampleRate / (double) kHop;
    const int minLag = juce::jmax (1, (int) std::floor (framesPerSecond * 60.0 / kMaxBpm));
    const int maxLag = (int) std::ceil (framesPerSecond * 60.0 / kMinBpm);
    if (maxLag >= numFrames / 2)
        return 0.0;

    int bestLag = 0;
    double bestScore = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double score = 0.0;
        for (int i = 0; i + lag < numFrames; ++i)
            score += flux[(size_t) i] * flux[(size_t) (i + lag)];
        // Slight bias towards shorter lags (faster tempi) to break harmonics ties
        score /= std::sqrt ((double) lag);
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestScore <= 0.0)
        return 0.0;

    double bpm = 60.0 * framesPerSecond / (double) bestLag;

    // Octave fold into the musically common range [70, 180)
    while (bpm < 70.0)  bpm *= 2.0;
    while (bpm >= 180.0) bpm *= 0.5;

    return juce::jlimit (kMinBpm, kMaxBpm, bpm);
}

double TempoDetector::detectBpmInFile (const juce::File& audioFile)
{
    if (! audioFile.existsAsFile())
        return 0.0;

    juce::AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return 0.0;

    const auto numSamples = (int) juce::jmin<juce::int64> (
        reader->lengthInSamples, (juce::int64) (reader->sampleRate * kMaxAnalysisS));
    if (numSamples < kHop * 8)
        return 0.0;

    // Mono mixdown
    juce::AudioBuffer<float> stereo (juce::jmin (2, (int) reader->numChannels), numSamples);
    reader->read (&stereo, 0, numSamples, 0, true, stereo.getNumChannels() > 1);

    juce::AudioBuffer<float> mono (1, numSamples);
    mono.copyFrom (0, 0, stereo, 0, 0, numSamples);
    if (stereo.getNumChannels() > 1)
    {
        mono.addFrom (0, 0, stereo, 1, 0, numSamples);
        mono.applyGain (0.5f);
    }

    return detectBpm (mono, reader->sampleRate);
}

void TempoDetector::detectAsync (const juce::File& audioFile,
                                 std::function<void (double)> onResult)
{
    std::thread ([audioFile, onResult = std::move (onResult)]
    {
        const double bpm = detectBpmInFile (audioFile);
        juce::MessageManager::callAsync ([bpm, onResult] { onResult (bpm); });
    }).detach();
}
