// test_tempo_detector.cpp — Phase 2+: offline BPM estimation

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/TempoDetector.h"

namespace
{
    /** Synthesises a click track: short decaying noise bursts on every beat. */
    juce::AudioBuffer<float> makeClickTrack (double bpm, double seconds, double sampleRate)
    {
        juce::AudioBuffer<float> buf (1, (int) (seconds * sampleRate));
        buf.clear();
        juce::Random rng (42);

        const double beatPeriod = 60.0 / bpm * sampleRate;
        const int clickLen = (int) (0.02 * sampleRate);   // 20 ms burst

        for (double pos = 0.0; pos + clickLen < buf.getNumSamples(); pos += beatPeriod)
        {
            const int start = (int) pos;
            for (int i = 0; i < clickLen; ++i)
            {
                const float env = 1.0f - (float) i / (float) clickLen;
                buf.addSample (0, start + i, (rng.nextFloat() * 2.0f - 1.0f) * env * 0.8f);
            }
        }
        return buf;
    }
}

TEST_CASE ("TempoDetector: detects click-track tempo within 2 BPM", "[tempo]")
{
    const double sr = 44100.0;

    for (double bpm : { 90.0, 120.0, 140.0 })
    {
        auto clicks = makeClickTrack (bpm, 20.0, sr);
        const double detected = TempoDetector::detectBpm (clicks, sr);
        INFO ("expected " << bpm << ", detected " << detected);
        REQUIRE_THAT (detected, Catch::Matchers::WithinAbs (bpm, 2.0));
    }
}

TEST_CASE ("TempoDetector: silence and too-short input return 0", "[tempo]")
{
    const double sr = 44100.0;

    juce::AudioBuffer<float> silence (1, (int) (10.0 * sr));
    silence.clear();
    REQUIRE (TempoDetector::detectBpm (silence, sr) == 0.0);

    juce::AudioBuffer<float> tiny (1, 256);
    tiny.clear();
    REQUIRE (TempoDetector::detectBpm (tiny, sr) == 0.0);

    REQUIRE (TempoDetector::detectBpmInFile (juce::File ("/nonexistent/file.wav")) == 0.0);
}
