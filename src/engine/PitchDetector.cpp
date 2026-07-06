#include "PitchDetector.h"

#include <vector>
#include <algorithm>
#include <cmath>

namespace
{
    /**
     * Ein YIN-Frame: liefert f0 in Hz oder 0 (unvoiced).
     * x zeigt auf windowSize Samples; Integrationsfenster = windowSize/2.
     */
    float yinFrame (const float* x, int windowSize, double sampleRate,
                    float minHz, float maxHz, float threshold)
    {
        const int n      = windowSize / 2;                                  // Integrationslänge
        const int tauMin = std::max (2, (int) (sampleRate / maxHz));
        const int tauMax = std::min (n - 1, (int) (sampleRate / minHz));
        if (tauMax <= tauMin + 2)
            return 0.0f;

        // Differenzfunktion d(tau)
        std::vector<float> d ((size_t) tauMax + 1, 0.0f);
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float diff = x[i] - x[i + tau];
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }

        // CMNDF: d'(tau) = d(tau) * tau / cumsum(d)
        std::vector<float> nd ((size_t) tauMax + 1, 1.0f);
        float cum = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            cum += d[(size_t) tau];
            nd[(size_t) tau] = cum > 0.0f ? d[(size_t) tau] * (float) tau / cum : 1.0f;
        }

        // Erste Unterschreitung der Schwelle; darin das lokale Minimum nehmen
        int tauEst = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (nd[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && nd[(size_t) (tau + 1)] < nd[(size_t) tau])
                    ++tau;
                tauEst = tau;
                break;
            }
        }
        if (tauEst < 0)
            return 0.0f;   // unvoiced

        // Parabolische Interpolation um tauEst für Sub-Sample-Genauigkeit
        double betterTau = tauEst;
        if (tauEst > tauMin && tauEst < tauMax)
        {
            const float s0 = nd[(size_t) (tauEst - 1)];
            const float s1 = nd[(size_t) tauEst];
            const float s2 = nd[(size_t) (tauEst + 1)];
            const float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1.0e-12f)
                betterTau = tauEst + (s2 - s0) / denom;
        }

        return betterTau > 0.0 ? (float) (sampleRate / betterTau) : 0.0f;
    }
}

juce::Array<float> PitchDetector::detectPitchHz (const float* mono, int numSamples,
                                                 double sampleRate, Settings s)
{
    juce::Array<float> curve;
    if (mono == nullptr || numSamples < s.windowSize || sampleRate <= 0.0)
        return curve;

    for (int pos = 0; pos + s.windowSize <= numSamples; pos += s.hopSize)
        curve.add (yinFrame (mono + pos, s.windowSize, sampleRate,
                             s.minHz, s.maxHz, s.threshold));

    return curve;
}

juce::Array<float> PitchDetector::detectPitchHz (const float* mono, int numSamples,
                                                 double sampleRate)
{
    return detectPitchHz (mono, numSamples, sampleRate, Settings());
}

float PitchDetector::medianPitchHz (const juce::Array<float>& curve)
{
    std::vector<float> voiced;
    for (float v : curve)
        if (v > 0.0f)
            voiced.push_back (v);

    if (voiced.empty())
        return 0.0f;

    std::nth_element (voiced.begin(), voiced.begin() + (long) voiced.size() / 2, voiced.end());
    return voiced[voiced.size() / 2];
}
