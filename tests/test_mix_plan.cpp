// test_mix_plan.cpp — Phase 4 / Iteration 1, Slice 1: the KI-Mixing action
// vocabulary. MixAction/MixPlan are pure data — the MCP-ready command language
// the co-pilot speaks. These golden tests pin the stable action names (which a
// future LLM/MCP adapter depends on) and the human-readable descriptions.

#include <catch2/catch_test_macros.hpp>

#include "ai/MixAction.h"

using cloudes::MixAction;
using cloudes::MixActionType;
using cloudes::MixPlan;

TEST_CASE ("MixAction: stable MCP-ready names", "[mixplan]")
{
    REQUIRE (MixAction { MixActionType::SetTrackGain   }.name() == "set_track_gain");
    REQUIRE (MixAction { MixActionType::SetTrackPan    }.name() == "set_track_pan");
    REQUIRE (MixAction { MixActionType::HighPassFilter }.name() == "high_pass_filter");
    REQUIRE (MixAction { MixActionType::CarveBand      }.name() == "carve_band");
    REQUIRE (MixAction { MixActionType::BoostBand      }.name() == "boost_band");
    REQUIRE (MixAction { MixActionType::Compress       }.name() == "compress");
}

TEST_CASE ("MixAction: describe carries the target and key parameter", "[mixplan]")
{
    MixAction a;
    a.type       = MixActionType::HighPassFilter;
    a.targetName = "Bass";
    a.freqHz     = 30.0f;

    const auto d = a.describe();
    REQUIRE (d.contains ("Bass"));
    REQUIRE (d.contains ("30"));
    REQUIRE (d.contains (juce::String::fromUTF8 ("Hochpass")));
}

TEST_CASE ("MixAction: pan label reads centre vs side", "[mixplan]")
{
    MixAction a;
    a.type       = MixActionType::SetTrackPan;
    a.targetName = "Gtr";

    a.pan = -0.5f;
    REQUIRE (a.describe().contains ("L"));

    a.pan = 0.0f;
    REQUIRE (a.describe().contains (juce::String::fromUTF8 ("Mitte")));
}

TEST_CASE ("MixPlan: aggregation", "[mixplan]")
{
    MixPlan p;
    REQUIRE (p.isEmpty());
    REQUIRE (p.size() == 0);

    MixAction a;
    a.type   = MixActionType::SetTrackGain;
    a.gainDb = -3.0f;

    p.add (a);
    p.add (a);

    REQUIRE_FALSE (p.isEmpty());
    REQUIRE (p.size() == 2);
    REQUIRE (p.actions.front().name() == "set_track_gain");
}
