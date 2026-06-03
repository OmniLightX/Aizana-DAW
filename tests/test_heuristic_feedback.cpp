// test_heuristic_feedback.cpp — Phase 2, Block B: HeuristicFeedbackEngine
//
// Tests the PURE rule function evaluate(MeterSnapshot) — no audio hardware needed.

#include <catch2/catch_test_macros.hpp>
#include "ai/HeuristicFeedbackEngine.h"

namespace
{
    using FB   = HeuristicFeedbackEngine;
    using Snap = HeuristicFeedbackEngine::MeterSnapshot;

    bool hasIntent (const std::vector<FB::Feedback>& v, IntentEngine::Intent i)
    {
        for (auto& f : v) if (f.hasIntent && f.suggestedIntent == i) return true;
        return false;
    }
    bool anyMessageContains (const std::vector<FB::Feedback>& v, const juce::String& s)
    {
        for (auto& f : v) if (f.message.containsIgnoreCase (s)) return true;
        return false;
    }
}

TEST_CASE ("HeuristicFeedback: silence yields a single intent-less prompt", "[ai][phase2][feedback]")
{
    auto v = FB::evaluate (Snap { -100.0f, -100.0f, -100.0f, -100.0f });
    REQUIRE (v.size() == 1);
    CHECK_FALSE (v[0].hasIntent);
    CHECK (anyMessageContains (v, "Play"));
}

TEST_CASE ("HeuristicFeedback: a quiet mix suggests matching a Pop reference", "[ai][phase2][feedback]")
{
    // peak -6, rms -16, lufs -16: quieter than Pop, not clipping, not very dynamic
    auto v = FB::evaluate (Snap { -6.0f, -16.0f, -15.0f, -16.0f });
    CHECK (hasIntent (v, IntentEngine::Intent::MatchPopReference));
}

TEST_CASE ("HeuristicFeedback: near-clipping suggests glue to tame peaks", "[ai][phase2][feedback]")
{
    auto v = FB::evaluate (Snap { -0.5f, -8.0f, -7.0f, -10.0f });
    CHECK (hasIntent (v, IntentEngine::Intent::GlueMix));
}

TEST_CASE ("HeuristicFeedback: very dynamic mix suggests glue", "[ai][phase2][feedback]")
{
    // crest = peak - rms = -3 - (-25) = 22 dB (> 18)
    auto v = FB::evaluate (Snap { -3.0f, -25.0f, -10.0f, -14.0f });
    CHECK (hasIntent (v, IntentEngine::Intent::GlueMix));
}

TEST_CASE ("HeuristicFeedback: a well-balanced mix gets positive, intent-less feedback", "[ai][phase2][feedback]")
{
    // peak -3, rms -14 (crest 11), lufs -10: not clipping, loud enough, controlled
    auto v = FB::evaluate (Snap { -3.0f, -14.0f, -10.0f, -10.0f });
    REQUIRE (v.size() == 1);
    CHECK_FALSE (v[0].hasIntent);
}
