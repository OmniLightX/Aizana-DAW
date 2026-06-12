// test_session_planner.cpp — Phase 3, Iteration 1, Slice 2 (AI-02/AI-03):
// pure planning layer — StemInfo in, SessionPlan out, no TE, no I/O.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/SessionPlanner.h"

namespace
{
    StemInfo stem (const char* path, const char* instrument, const char* group,
                   double rmsDb, double confidence = 0.9)
    {
        StemInfo s;
        s.file         = juce::File (path);
        s.instrument   = instrument;
        s.group        = group;
        s.confidence   = confidence;
        s.fromFilename = true;
        s.rmsDb        = rmsDb;
        s.peakDb       = rmsDb + 12.0;
        return s;
    }
}

TEST_CASE ("SessionPlanner: groups follow mixer order, only with members", "[planner]")
{
    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/vox.wav",   "Vocals", "Vocals", -20.0));
    stems.add (stem ("/t/kick.wav",  "Kick",   "Drums",  -20.0));
    stems.add (stem ("/t/808.wav",   "808",    "Bass",   -20.0));
    stems.add (stem ("/t/misc.wav",  "Audio",  "Other",  -20.0));

    const auto plan = SessionPlanner::plan (stems);

    // Mixer convention order, no empty groups (no Guitars/Keys/FX), no "Other" bus
    REQUIRE (plan.groups.size() == 3);
    CHECK (plan.groups[0] == "Drums");
    CHECK (plan.groups[1] == "Bass");
    CHECK (plan.groups[2] == "Vocals");

    // "Other" stems route directly to master (empty group)
    REQUIRE (plan.tracks.size() == 4);
    CHECK (plan.tracks[3].group.isEmpty());
    CHECK (plan.tracks[0].group == "Vocals");
}

TEST_CASE ("SessionPlanner: duplicate instrument names get numeric suffixes", "[planner]")
{
    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/gtr1.wav", "Guitar", "Guitars", -20.0));
    stems.add (stem ("/t/gtr2.wav", "Guitar", "Guitars", -20.0));
    stems.add (stem ("/t/gtr3.wav", "Guitar", "Guitars", -20.0));

    const auto plan = SessionPlanner::plan (stems);
    REQUIRE (plan.tracks.size() == 3);
    CHECK (plan.tracks[0].name == "Guitar");
    CHECK (plan.tracks[1].name == "Guitar 2");
    CHECK (plan.tracks[2].name == "Guitar 3");
}

TEST_CASE ("SessionPlanner: gain staging targets -18 dBFS RMS (vocals -16)", "[planner]")
{
    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/kick.wav", "Kick",   "Drums",  -30.0));  // quiet → boost
    stems.add (stem ("/t/bass.wav", "Bass",   "Bass",   -10.0));  // hot   → cut
    stems.add (stem ("/t/vox.wav",  "Vocals", "Vocals", -26.0));  // vocal target -16

    const auto plan = SessionPlanner::plan (stems);

    // 3 active tracks → estimated sum -18 + 10*log10(3) = -13.2 dB, within the
    // -12 dB budget → no trim. Gains hit the per-track targets exactly.
    REQUIRE_THAT (plan.tracks[0].gainDb, Catch::Matchers::WithinAbs (12.0, 0.01)); // -30→-18
    REQUIRE_THAT (plan.tracks[1].gainDb, Catch::Matchers::WithinAbs (-8.0, 0.01)); // -10→-18
    REQUIRE_THAT (plan.tracks[2].gainDb, Catch::Matchers::WithinAbs (10.0, 0.01)); // -26→-16
}

TEST_CASE ("SessionPlanner: gain clamps, silence never boosted", "[planner]")
{
    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/whisper.wav", "Vocals", "Vocals", -55.0));  // would need +39
    stems.add (stem ("/t/silence.wav", "Audio",  "Other", -120.0));  // silent

    const auto plan = SessionPlanner::plan (stems);

    // Clamped to +12 max (one active track → no trim)
    REQUIRE_THAT (plan.tracks[0].gainDb,
                  Catch::Matchers::WithinAbs (SessionPlanner::kMaxGainDb, 0.01));
    // Silence: gain stays 0 — never boost noise floors
    REQUIRE_THAT (plan.tracks[1].gainDb, Catch::Matchers::WithinAbs (0.0, 0.01));
}

TEST_CASE ("SessionPlanner: many tracks trigger equal headroom trim", "[planner]")
{
    // 16 tracks at -18 → estimated sum -18 + 10*log10(16) = -5.96 dB.
    // Budget -12 dB → every gain trimmed by ~6.04 dB, balance preserved.
    juce::Array<StemInfo> stems;
    for (int i = 0; i < 16; ++i)
        stems.add (stem (("/t/d" + juce::String (i) + ".wav").toRawUTF8(),
                         "Drums", "Drums", -18.0));

    const auto plan = SessionPlanner::plan (stems);
    const double expectedTrim = (SessionPlanner::kTargetRmsDb + 10.0 * std::log10 (16.0))
                                - SessionPlanner::kMaxSumRmsDb;

    for (const auto& t : plan.tracks)
        REQUIRE_THAT (t.gainDb, Catch::Matchers::WithinAbs (-expectedTrim, 0.01));

    // Relative balance: all equal here, so max-min spread must be ~0
    double mn = 1000, mx = -1000;
    for (const auto& t : plan.tracks) { mn = std::min (mn, t.gainDb); mx = std::max (mx, t.gainDb); }
    REQUIRE_THAT (mx - mn, Catch::Matchers::WithinAbs (0.0, 0.001));
}

TEST_CASE ("SessionPlanner: colours stable per group, plan is deterministic", "[planner]")
{
    CHECK (SessionPlanner::colourForGroup ("Drums")  != SessionPlanner::colourForGroup ("Bass"));
    CHECK (SessionPlanner::colourForGroup ("Vocals") != SessionPlanner::colourForGroup ("FX"));
    CHECK (SessionPlanner::colourForGroup ("Drums")  == SessionPlanner::colourForGroup ("Drums"));

    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/kick.wav", "Kick", "Drums", -20.0));
    const auto a = SessionPlanner::plan (stems);
    const auto b = SessionPlanner::plan (stems);
    REQUIRE (a.tracks.size() == b.tracks.size());
    CHECK (a.tracks[0].name == b.tracks[0].name);
    CHECK (a.tracks[0].gainDb == b.tracks[0].gainDb);
    CHECK (a.tracks[0].colour == b.tracks[0].colour);
}
