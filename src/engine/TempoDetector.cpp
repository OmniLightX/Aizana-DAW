#include "TempoDetector.h"

#include <thread>
#include <vector>
#include <cmath>

namespace
{
    constexpr int    kHop          = 512;     // onset envelope frame hop (samples)
    constexpr double kMaxAnalysisS = 60.0;    // analyse at most the first minute
    constexpr double kMinBpm       = 60.0;
    constexpr double kMaxBpm       = 200.0;

    // Onset-strength envelope: rectified frame-energy difference (one value per hop)
    std::vector<double> computeOnsetFlux (const juce::AudioBuffer<float>& mono)
    {
        const float* in = mono.getReadPointer (0);
        const int numFrames = mono.getNumSamples() / kHop;

        std::vector<double> flux ((size_t) juce::jmax (0, numFrames), 0.0);
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
        return flux;
    }

    // Global pulse estimate from the (raw) flux via autocorrelation; 0.0 = none
    double bpmFromFlux (std::vector<double> flux, double framesPerSecond)
    {
        const int numFrames = (int) flux.size();
        if (numFrames < 16)
            return 0.0;

        // Remove DC of the envelope so autocorrelation peaks on periodicity
        double mean = 0.0;
        for (double v : flux) mean += v;
        mean /= (double) flux.size();
        for (double& v : flux) v -= mean;

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

    // Mono mixdown of the first kMaxAnalysisS seconds; empty buffer on failure
    juce::AudioBuffer<float> loadMonoForAnalysis (const juce::File& audioFile)
    {
        if (! audioFile.existsAsFile())
            return {};

        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return {};

        const auto numSamples = (int) juce::jmin<juce::int64> (
            reader->lengthInSamples, (juce::int64) (reader->sampleRate * kMaxAnalysisS));
        if (numSamples < kHop * 8)
            return {};

        juce::AudioBuffer<float> stereo (juce::jmin (2, (int) reader->numChannels), numSamples);
        reader->read (&stereo, 0, numSamples, 0, true, stereo.getNumChannels() > 1);

        juce::AudioBuffer<float> mono (1, numSamples);
        mono.copyFrom (0, 0, stereo, 0, 0, numSamples);
        if (stereo.getNumChannels() > 1)
        {
            mono.addFrom (0, 0, stereo, 1, 0, numSamples);
            mono.applyGain (0.5f);
        }
        return mono;
    }

    double readerSampleRate (const juce::File& audioFile)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        return reader != nullptr ? reader->sampleRate : 0.0;
    }
}

//==============================================================================
double TempoDetector::detectBpm (const juce::AudioBuffer<float>& mono, double sampleRate)
{
    if (sampleRate <= 0.0 || mono.getNumSamples() < kHop * 8)
        return 0.0;

    return bpmFromFlux (computeOnsetFlux (mono), sampleRate / (double) kHop);
}

double TempoDetector::detectBpmInFile (const juce::File& audioFile)
{
    const auto mono = loadMonoForAnalysis (audioFile);
    if (mono.getNumSamples() == 0)
        return 0.0;

    return detectBpm (mono, readerSampleRate (audioFile));
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

//==============================================================================
// P7a: Beat-Tracking (Ellis 2007 dynamic programming)

juce::Array<double> TempoDetector::detectBeats (const juce::AudioBuffer<float>& mono,
                                                double sampleRate)
{
    if (sampleRate <= 0.0 || mono.getNumSamples() < kHop * 8)
        return {};

    const double framesPerSecond = sampleRate / (double) kHop;
    auto flux = computeOnsetFlux (mono);
    const int n = (int) flux.size();
    if (n < 16)
        return {};

    const double bpm = bpmFromFlux (flux, framesPerSecond);
    if (bpm <= 0.0)
        return {};

    const double tau = framesPerSecond * 60.0 / bpm;   // frames per beat (global)

    // Normalise onset strength to [0, 1]
    double maxFlux = 0.0;
    for (double v : flux) maxFlux = juce::jmax (maxFlux, v);
    if (maxFlux <= 0.0)
        return {};
    for (double& v : flux) v /= maxFlux;

    // DP (Ellis 2007, adaptiv): C(t) = O(t) + max(0, max_Δ [ C(t−Δ) − Strafe ]).
    // Die Strafe referenziert das INTERVALL DES VORGÄNGERS (Tempo darf driften/
    // springen — einmal teuer, danach billig) plus einen leichten Zug zum
    // globalen Puls τ (verhindert Subdivision-/Doppeltempo-Drift). Eine rein
    // globale Referenz zwang bei echten Tempowechseln (100→150) das Raster
    // auf Zwischenwerte — real im Test gefangen.
    // β klein halten: ein starker globaler Zug machte Halb-Tempo-Tracking nach
    // einem echten Tempowechsel billiger als das Klick-für-Klick-Folgen (real
    // im 100→150-Test gefangen — zweite Hälfte rastete auf 75 BPM ein).
    constexpr double alphaLocal  = 4.0;
    constexpr double betaGlobal  = 0.3;
    const int dMin = juce::jmax (1, (int) std::floor (tau * 0.40));
    const int dMax = juce::jmax (dMin + 1, (int) std::ceil (tau * 2.2));

    std::vector<double> score ((size_t) n, 0.0);
    std::vector<int>    backlink ((size_t) n, -1);
    std::vector<int>    interval ((size_t) n, 0);   // Δ, das zu t führte (0 = Start)

    for (int t = 0; t < n; ++t)
    {
        double best = 0.0;
        int bestPrev = -1;
        int bestD = 0;

        for (int d = dMin; d <= juce::jmin (dMax, t); ++d)
        {
            const int prev = t - d;
            const double refLocal = interval[(size_t) prev] > 0
                                      ? (double) interval[(size_t) prev]
                                      : tau;

            const double logLocal  = std::log ((double) d / refLocal);
            const double logGlobal = std::log ((double) d / tau);
            const double cand = score[(size_t) prev]
                              - alphaLocal * logLocal  * logLocal
                              - betaGlobal * logGlobal * logGlobal;
            if (cand > best)
            {
                best = cand;
                bestPrev = prev;
                bestD = d;
            }
        }

        score[(size_t) t]    = flux[(size_t) t] + best;
        backlink[(size_t) t] = bestPrev;
        interval[(size_t) t] = bestD;
    }

    // Endpunkt: bestes C im letzten 1.5·τ-Fenster, dann Backtrack
    int end = -1;
    double bestEnd = -1.0;
    for (int t = juce::jmax (0, n - (int) std::ceil (tau * 1.5)); t < n; ++t)
    {
        if (score[(size_t) t] > bestEnd)
        {
            bestEnd = score[(size_t) t];
            end = t;
        }
    }
    if (end < 0)
        return {};

    juce::Array<double> beats;
    for (int t = end; t >= 0; t = backlink[(size_t) t])
    {
        beats.add ((double) t * (double) kHop / sampleRate);
        if (backlink[(size_t) t] < 0)
            break;
    }

    // Backtrack lief rückwärts; weniger als 2 Beats = kein verwertbarer Puls
    std::reverse (beats.begin(), beats.end());
    if (beats.size() < 2)
        return {};

    return beats;
}

juce::Array<double> TempoDetector::detectBeatsInFile (const juce::File& audioFile)
{
    const auto mono = loadMonoForAnalysis (audioFile);
    if (mono.getNumSamples() == 0)
        return {};

    return detectBeats (mono, readerSampleRate (audioFile));
}

void TempoDetector::detectBeatsAsync (const juce::File& audioFile,
                                      std::function<void (juce::Array<double>)> onResult)
{
    std::thread ([audioFile, onResult = std::move (onResult)]
    {
        auto beats = detectBeatsInFile (audioFile);
        juce::MessageManager::callAsync ([beats = std::move (beats), onResult]
                                         { onResult (beats); });
    }).detach();
}
