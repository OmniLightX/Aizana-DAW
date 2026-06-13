// test_mixing_engine.cpp — Phase 4 / Iteration 1, Slice 2: the heuristic mixing
// brain. MixingEngine::mix is pure (TrackMixProfile in → MixPlan out), so these
// golden tests pin the Grammy-oriented rules deterministically: gain staging,
// subtractive-first EQ, forward de-essed/serially-compressed vocals, role-based
// panning, genre nudges (± clamped, REF-04), and the reviewable master glue.

#include <catch2/catch_test_macros.hpp>

#include "ai/MixingEngine.h"

using cloudes::MixAction;
using cloudes::MixActionType;
using cloudes::MixPlan;
using cloudes::MixingEngine;
using cloudes::TrackMixProfile;

namespace
{
    TrackMixProfile track (juce::String name, juce::String group, double rmsDb,
                           bool lead = false, float pan = 0.0f)
    {
        TrackMixProfile t;
        t.name        = std::move (name);
        t.group       = std::move (group);
        t.rmsDb       = rmsDb;
        t.peakDb      = rmsDb + 10.0;
        t.isLeadVocal = lead;
        t.currentPan  = pan;
        return t;
    }

    // Does the plan contain an action of `type` targeting `name`?
    bool has (const MixPlan& p, MixActionType type, juce::String name)
    {
        for (const auto& a : p.actions)
            if (a.type == type && a.targetName == name)
                return true;
        return false;
    }

    int count (const MixPlan& p, MixActionType type, juce::String name)
    {
        int n = 0;
        for (const auto& a : p.actions)
            if (a.type == type && a.targetName == name)
                ++n;
        return n;
    }

    const MixAction* find (const MixPlan& p, MixActionType type, juce::String name)
    {
        for (const auto& a : p.actions)
            if (a.type == type && a.targetName == name)
                return &a;
        return nullptr;
    }
}

TEST_CASE ("MixingEngine: empty session yields an empty plan", "[mixing]")
{
    const auto plan = MixingEngine::mix ({});
    REQUIRE (plan.isEmpty());
}

TEST_CASE ("MixingEngine: role mapping", "[mixing]")
{
    using R = MixingEngine::Role;
    REQUIRE (MixingEngine::roleForGroup ("Drums")  == R::Drums);
    REQUIRE (MixingEngine::roleForGroup ("Bass")   == R::Bass);
    REQUIRE (MixingEngine::roleForGroup ("Vocals") == R::Vocals);
    REQUIRE (MixingEngine::roleForGroup ("Synths") == R::Synths);
    REQUIRE (MixingEngine::roleForGroup ("")       == R::Other);
    REQUIRE (MixingEngine::roleForGroup ("Banjo")  == R::Other);
}

TEST_CASE ("MixingEngine: lead vocal gets the full Grammy chain", "[mixing]")
{
    std::vector<TrackMixProfile> tracks {
        track ("Lead Vox", "Vocals", -24.0, /*lead*/ true),
        track ("Bass",     "Bass",   -20.0),
        track ("Drums",    "Drums",  -18.0),
        track ("Gtr",      "Guitars", -22.0),
        track ("Synth",    "Synths",  -22.0),
    };

    const auto plan = MixingEngine::mix (tracks);

    // De-ess + presence bell + air shelf, all on the lead vocal.
    REQUIRE (has (plan, MixActionType::DeEss,     "Lead Vox"));
    REQUIRE (has (plan, MixActionType::BoostBand, "Lead Vox"));
    REQUIRE (has (plan, MixActionType::HighShelf, "Lead Vox"));

    // Serial-lite compression = exactly two compressors on the vocal.
    REQUIRE (count (plan, MixActionType::Compress, "Lead Vox") == 2);

    // Air is a high-shelf, never a bell.
    const auto* air = find (plan, MixActionType::HighShelf, "Lead Vox");
    REQUIRE (air != nullptr);
    REQUIRE (air->freqHz == MixingEngine::kVocalAirHz);

    // Presence bell is wide (low Q) per user feedback.
    const auto* pres = find (plan, MixActionType::BoostBand, "Lead Vox");
    REQUIRE (pres != nullptr);
    REQUIRE (pres->q == MixingEngine::kVocalPresenceQ);
}

TEST_CASE ("MixingEngine: low cut on non-bass, never on bass or drums", "[mixing]")
{
    std::vector<TrackMixProfile> tracks {
        track ("Bass",  "Bass",    -18.0),
        track ("Drums", "Drums",   -18.0),
        track ("Gtr",   "Guitars", -18.0),
        track ("Keys",  "Keys",    -18.0),
    };

    const auto plan = MixingEngine::mix (tracks);

    REQUIRE      (has (plan, MixActionType::HighPassFilter, "Gtr"));
    REQUIRE      (has (plan, MixActionType::HighPassFilter, "Keys"));
    REQUIRE_FALSE (has (plan, MixActionType::HighPassFilter, "Bass"));
    REQUIRE_FALSE (has (plan, MixActionType::HighPassFilter, "Drums"));
}

TEST_CASE ("MixingEngine: anchors centre, others spread", "[mixing]")
{
    std::vector<TrackMixProfile> tracks {
        track ("Lead Vox", "Vocals",  -18.0, true,  /*pan*/ 0.6f),  // off-centre → pulled in
        track ("Bass",     "Bass",    -18.0),
        track ("Synth A",  "Synths",  -18.0),
        track ("Synth B",  "Synths",  -18.0),
    };

    const auto plan = MixingEngine::mix (tracks);

    // Lead vocal that was panned 0.6 gets re-centred.
    const auto* vox = find (plan, MixActionType::SetTrackPan, "Lead Vox");
    REQUIRE (vox != nullptr);
    REQUIRE (vox->pan == 0.0f);

    // Two synths get spread to opposite sides.
    const auto* a = find (plan, MixActionType::SetTrackPan, "Synth A");
    const auto* b = find (plan, MixActionType::SetTrackPan, "Synth B");
    REQUIRE (a != nullptr);
    REQUIRE (b != nullptr);
    REQUIRE ((a->pan < 0.0f) != (b->pan < 0.0f));   // opposite sides
}

TEST_CASE ("MixingEngine: master glue is opt-out", "[mixing]")
{
    std::vector<TrackMixProfile> tracks { track ("Drums", "Drums", -18.0) };

    const auto withGlue = MixingEngine::mix (tracks);
    REQUIRE (has (withGlue, MixActionType::Compress, "Master"));

    MixingEngine::Options noGlue;
    noGlue.allowMasterGlue = false;
    const auto without = MixingEngine::mix (tracks, nullptr, noGlue);
    REQUIRE_FALSE (has (without, MixActionType::Compress, "Master"));
}

TEST_CASE ("MixingEngine: reference nudges stay clamped (REF-04)", "[mixing]")
{
    // An absurd profile must never push a move past the engine's clamps.
    ReferenceProfile extreme;
    extreme.name    = "Trap / Phonk";
    extreme.vocalDb = 99.0;
    extreme.highDb  = 99.0;
    extreme.midDb   = -99.0;
    extreme.lowDb   = 99.0;

    std::vector<TrackMixProfile> tracks {
        track ("Lead Vox", "Vocals", -40.0, true),   // very quiet → wants a big boost
        track ("Bass",     "Bass",   -18.0),
        track ("Synth",    "Synths", -18.0),
    };

    const auto plan = MixingEngine::mix (tracks, &extreme);

    const auto* gain = find (plan, MixActionType::SetTrackGain, "Lead Vox");
    REQUIRE (gain != nullptr);
    REQUIRE (gain->gainDb <= (float) MixingEngine::kMaxGainDb);

    const auto* air = find (plan, MixActionType::HighShelf, "Lead Vox");
    REQUIRE (air != nullptr);
    REQUIRE (air->gainDb <= 3.0f);          // air shelf clamp

    const auto* mud = find (plan, MixActionType::CarveBand, "Lead Vox");
    REQUIRE (mud != nullptr);
    REQUIRE (mud->gainDb >= -4.0f);         // mud carve clamp
    REQUIRE (mud->gainDb <  0.0f);          // it's a cut
}
