// test_mix_data_logger.cpp — the training-data flywheel. Verifies the pure JSONL core
// and the accept/reject verdict logging that Slice 5b will later distil a model from.
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "ai/MixDataLogger.h"
#include "ai/MixFeatures.h"

using aizana::MixAction;
using aizana::MixActionType;
using aizana::MixDataLogger;
using aizana::MixLogContext;
using aizana::MixPlan;
using aizana::TrackMixProfile;

namespace
{
    TrackMixProfile track (juce::uint64 id, juce::String name, juce::String group,
                           double rms, bool lead = false)
    {
        TrackMixProfile t;
        t.id          = tracktion::engine::EditItemID::fromRawID (id);
        t.name        = std::move (name);
        t.group       = std::move (group);
        t.rmsDb       = rms;
        t.peakDb      = rms + 10.0;
        t.isLeadVocal = lead;
        return t;
    }

    MixAction action (MixActionType type, juce::uint64 trackId, juce::String name)
    {
        MixAction a;
        a.type        = type;
        a.targetTrack = tracktion::engine::EditItemID::fromRawID (trackId);
        a.targetName  = std::move (name);
        return a;
    }
}

TEST_CASE ("MixDataLogger: toJsonLine emits a parseable labelled example", "[datalog]")
{
    auto t = track (1, "Lead Vox", "Vocals", -16.0, true);
    auto a = action (MixActionType::SetTrackGain, 1, "Lead Vox");
    a.gainDb = 2.0f;
    a.reason = juce::String::fromUTF8 ("Lauter, damit die Spur sitzt");

    const auto line = MixDataLogger::toJsonLine (t, a, true, { "heuristic", "pro", "Hip-Hop/Rap", "" });

    REQUIRE_FALSE (line.containsChar ('\n'));     // single JSONL line
    const auto v = juce::JSON::parse (line);
    REQUIRE (v.isObject());
    REQUIRE ((bool) v["accepted"] == true);
    REQUIRE (v["proposer"].toString() == "heuristic");
    REQUIRE (v["mode"].toString() == "pro");
    REQUIRE (v["action"]["name"].toString() == "set_track_gain");
    REQUIRE (v["track"]["group"].toString() == "Vocals");
    REQUIRE ((bool) v["track"]["lead"] == true);
    REQUIRE (v["features"]["gain_trim"].isArray());
    REQUIRE (v["features"]["gain_trim"].size()
             == (int) aizana::mixfeatures::GainTrimCount);
}

TEST_CASE ("MixDataLogger: logReview records verdicts and appends", "[datalog]")
{
    const std::vector<TrackMixProfile> profiles {
        track (1, "Lead Vox", "Vocals", -16.0, true),
        track (2, "Kick",     "Drums",  -18.0),
    };

    MixPlan proposed;
    proposed.add (action (MixActionType::SetTrackGain, 1, "Lead Vox"));   // accepted
    proposed.add (action (MixActionType::DeEss,        1, "Lead Vox"));   // rejected
    proposed.add (action (MixActionType::SetTrackGain, 2, "Kick"));       // accepted

    MixPlan accepted;                       // Pro user kept gain moves, dropped the de-ess
    accepted.add (proposed.actions[0]);
    accepted.add (proposed.actions[2]);

    auto logFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_mixlog_test.jsonl");
    logFile.deleteFile();

    const int n = MixDataLogger::logReview (profiles, proposed, accepted,
                                            { "heuristic", "pro", "", "" }, logFile);
    REQUIRE (n == 3);

    juce::StringArray lines;
    lines.addLines (logFile.loadFileAsString());
    lines.removeEmptyStrings();
    REQUIRE (lines.size() == 3);

    int acceptedCount = 0, deEssAccepted = -1;
    for (const auto& l : lines)
    {
        const auto v = juce::JSON::parse (l);
        if ((bool) v["accepted"]) ++acceptedCount;
        if (v["action"]["name"].toString() == "de_ess")
            deEssAccepted = (bool) v["accepted"] ? 1 : 0;
    }
    REQUIRE (acceptedCount == 2);     // both gain moves
    REQUIRE (deEssAccepted == 0);     // the rejected de-ess is labelled rejected

    // append semantics: a second review grows the corpus, never truncates
    MixDataLogger::logReview (profiles, proposed, proposed,
                              { "heuristic", "beginner", "", "" }, logFile);
    juce::StringArray after;
    after.addLines (logFile.loadFileAsString());
    after.removeEmptyStrings();
    REQUIRE (after.size() == 6);

    logFile.deleteFile();
}
