// test_mix_plan.cpp — Phase 4 / Iteration 1, Slice 1: the KI-Mixing action
// vocabulary. MixAction/MixPlan are pure data — the MCP-ready command language
// the co-pilot speaks. These golden tests pin the stable action names (which a
// future LLM/MCP adapter depends on) and the human-readable descriptions.

#include <catch2/catch_test_macros.hpp>

#include "ai/MixAction.h"

using aizana::MixAction;
using aizana::MixActionType;
using aizana::MixPlan;

TEST_CASE ("MixAction: stable MCP-ready names", "[mixplan]")
{
    REQUIRE (MixAction { MixActionType::SetTrackGain   }.name() == "set_track_gain");
    REQUIRE (MixAction { MixActionType::SetTrackPan    }.name() == "set_track_pan");
    REQUIRE (MixAction { MixActionType::HighPassFilter }.name() == "high_pass_filter");
    REQUIRE (MixAction { MixActionType::CarveBand      }.name() == "carve_band");
    REQUIRE (MixAction { MixActionType::BoostBand      }.name() == "boost_band");
    REQUIRE (MixAction { MixActionType::LowShelf       }.name() == "low_shelf");
    REQUIRE (MixAction { MixActionType::HighShelf      }.name() == "high_shelf");
    REQUIRE (MixAction { MixActionType::Compress       }.name() == "compress");
    REQUIRE (MixAction { MixActionType::DeEss          }.name() == "de_ess");
}

TEST_CASE ("MixAction: shelves and compressor makeup describe", "[mixplan]")
{
    MixAction air;
    air.type       = MixActionType::HighShelf;
    air.targetName = "Vocals";
    air.freqHz     = 12000.0f;
    air.gainDb     = 1.5f;
    const auto da = air.describe();
    REQUIRE (da.contains (juce::String::fromUTF8 ("High-Shelf")));
    REQUIRE (da.contains ("+1.5"));
    REQUIRE (da.contains ("12000"));

    MixAction comp;
    comp.type         = MixActionType::Compress;
    comp.targetName   = "Vocals";
    comp.ratio        = 4.0f;
    comp.thresholdDb  = -19.0f;
    comp.makeupGainDb = 2.0f;
    const auto dc = comp.describe();
    REQUIRE (dc.contains ("4.0:1"));
    REQUIRE (dc.contains ("-19.0"));
    REQUIRE (dc.contains ("Makeup"));
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
