// test_delay_dsp.cpp — P2 (S1-Parität): Built-in Delay (Zeit, Feedback, Mix, Sync)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fx/DelayProcessor.h>
#include <cmath>

namespace
{
    // Impulse into a stereo buffer, then silence
    juce::AudioBuffer<float> makeImpulseBuffer (int numSamples)
    {
        juce::AudioBuffer<float> buf (2, numSamples);
        buf.clear();
        buf.setSample (0, 0, 1.0f);
        buf.setSample (1, 0, 1.0f);
        return buf;
    }

    // Index of the absolute peak in channel 0 of [from, to)
    int peakIndex (const juce::AudioBuffer<float>& buf, int from, int to)
    {
        int best = from;
        float bestVal = 0.0f;
        for (int i = from; i < to; ++i)
        {
            const float v = std::abs (buf.getSample (0, i));
            if (v > bestVal) { bestVal = v; best = i; }
        }
        return best;
    }
}

TEST_CASE ("Delay: impulse repeats at the configured time", "[delay][P2]")
{
    DelayProcessor delay;
    delay.setTimeMs (100.0f);
    delay.setFeedback (0.0f);
    delay.setMix (100.0f);   // wet only → repeat clearly measurable

    const double sr = 44100.0;
    const int total = 22050;   // 0.5 s
    delay.prepareToPlay (sr, total);

    auto buf = makeImpulseBuffer (total);
    juce::MidiBuffer midi;
    delay.processBlock (buf, midi);

    // Expected echo at 100 ms = 4410 samples (± smoothing/interp tolerance)
    const int idx = peakIndex (buf, 1000, total);
    CHECK (std::abs (idx - 4410) < 100);
    CHECK (std::abs (buf.getSample (0, idx)) > 0.25f);
}

TEST_CASE ("Delay: feedback produces a decaying second repeat", "[delay][P2]")
{
    DelayProcessor delay;
    delay.setTimeMs (100.0f);
    delay.setFeedback (50.0f);
    delay.setMix (100.0f);

    const double sr = 44100.0;
    const int total = 44100;   // 1 s → room for 2 repeats
    delay.prepareToPlay (sr, total);

    auto buf = makeImpulseBuffer (total);
    juce::MidiBuffer midi;
    delay.processBlock (buf, midi);

    const float first  = std::abs (buf.getSample (0, peakIndex (buf, 3000, 6000)));
    const float second = std::abs (buf.getSample (0, peakIndex (buf, 7500, 10500)));

    CHECK (first > 0.25f);
    CHECK (second > 0.05f);
    CHECK (second < first);   // repeats decay (feedback < 100 % + loop filters)
}

TEST_CASE ("Delay: mix 0 % leaves the dry signal untouched", "[delay][P2]")
{
    DelayProcessor delay;
    delay.setTimeMs (100.0f);
    delay.setFeedback (50.0f);
    delay.setMix (0.0f);

    const int total = 8192;
    delay.prepareToPlay (44100.0, total);

    auto buf = makeImpulseBuffer (total);
    juce::MidiBuffer midi;
    delay.processBlock (buf, midi);

    CHECK_THAT (buf.getSample (0, 0), Catch::Matchers::WithinAbs (1.0f, 1e-5f));
    // No audible echo in the dry-only output
    CHECK (std::abs (buf.getSample (0, 4410)) < 1e-3f);
}

TEST_CASE ("Delay: sync divisions map to beats", "[delay][P2]")
{
    CHECK (DelayProcessor::beatsForSyncIndex (0) == 0.0);              // free
    CHECK (DelayProcessor::beatsForSyncIndex (4) == 1.0);              // 1/4
    CHECK (DelayProcessor::beatsForSyncIndex (7) == 0.5);              // 1/8
    CHECK (DelayProcessor::beatsForSyncIndex (10) == 0.25);            // 1/16
    CHECK_THAT (DelayProcessor::beatsForSyncIndex (6),
                Catch::Matchers::WithinAbs (2.0 / 3.0, 1e-9));         // 1/4T
    CHECK (DelayProcessor::syncChoices().size() == 11);
}
