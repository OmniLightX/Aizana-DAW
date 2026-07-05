// test_reference_profile.cpp — Phase 3, Iteration 2 (REF-01/02/04):
// genre profiles, reference-track analysis, profile-aware gain staging.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/ReferenceAnalyzer.h"
#include "ai/SessionPlanner.h"

namespace
{
    constexpr double kSr = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("aizana_ref_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        REQUIRE (dir.createDirectory());
        return dir;
    }

    StemInfo stem (const char* path, const char* instrument, const char* group, double rmsDb)
    {
        StemInfo s;
        s.file = juce::File (path);
        s.instrument = instrument;
        s.group = group;
        s.confidence = 0.9;
        s.rmsDb = rmsDb;
        s.peakDb = rmsDb + 12.0;
        return s;
    }
}

TEST_CASE ("ReferenceProfile: genre presets + ValueTree roundtrip", "[reference]")
{
    for (const auto& g : ReferenceProfile::genreNames())
    {
        const auto p = ReferenceProfile::genre (g);
        REQUIRE (p.isValid());
        CHECK (p.name == g);
        CHECK_FALSE (p.fromReferenceTrack);

        const auto restored = ReferenceProfile::fromValueTree (p.toValueTree());
        CHECK (restored.name == p.name);
        REQUIRE_THAT (restored.targetLufs, Catch::Matchers::WithinAbs (p.targetLufs, 0.001));
        REQUIRE_THAT (restored.lowDb,      Catch::Matchers::WithinAbs (p.lowDb, 0.001));
        REQUIRE_THAT (restored.vocalDb,    Catch::Matchers::WithinAbs (p.vocalDb, 0.001));
    }

    CHECK_FALSE (ReferenceProfile::genre ("Polka").isValid());
    CHECK_FALSE (ReferenceProfile::fromValueTree (juce::ValueTree ("WRONG")).isValid());

    // Modern conventions (user feedback 2026-06-13): rap vocals sit PRESENT in
    // the mix; Trap is the bass-heaviest; Podcast stays the vocal extreme.
    CHECK (ReferenceProfile::genre ("Hip-Hop / Rap").vocalDb >= 2.0);
    CHECK (ReferenceProfile::genre ("Podcast").vocalDb > ReferenceProfile::genre ("Rock").vocalDb);
    CHECK (ReferenceProfile::genre ("Trap / Phonk").lowDb >= ReferenceProfile::genre ("Hip-Hop / Rap").lowDb);
    CHECK (ReferenceProfile::genre ("Hip-Hop / Rap").lowDb > ReferenceProfile::genre ("Podcast").lowDb);
}

TEST_CASE ("ReferenceAnalyzer: band balance separates bass-heavy from bright material",
           "[reference]")
{
    const int n = (int) (10.0 * kSr);
    std::vector<float> bassHeavy ((size_t) n), bright ((size_t) n);
    juce::Random rng (11);

    for (int i = 0; i < n; ++i)
    {
        bassHeavy[(size_t) i] = 0.6f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * i / kSr)
                              + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);
        bright[(size_t) i]    = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 8000.0 * i / kSr)
                              + 0.05f * (rng.nextFloat() * 2.0f - 1.0f);
    }

    double l1, m1, h1, l2, m2, h2;
    ReferenceAnalyzer::measureBandBalance (bassHeavy.data(), n, kSr, l1, m1, h1);
    ReferenceAnalyzer::measureBandBalance (bright.data(),    n, kSr, l2, m2, h2);

    CHECK (l1 > 0.5);          // bass-heavy → low emphasis positive
    CHECK (l1 > h1);
    CHECK (h2 > 0.5);          // bright → high emphasis positive
    CHECK (h2 > l2);

    // Vocal presence (1-4 kHz): a presence-band tone reads positive, a sub
    // bass reads non-positive.
    std::vector<float> presence ((size_t) n), sub ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        presence[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2500.0 * i / kSr);
        sub[(size_t) i]      = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 55.0   * i / kSr);
    }
    CHECK (ReferenceAnalyzer::measureVocalPresence (presence.data(), n, kSr) > 0.5);
    CHECK (ReferenceAnalyzer::measureVocalPresence (sub.data(),      n, kSr) <= 0.0);
}

TEST_CASE ("ReferenceAnalyzer: analyzeFile measures LUFS and sets profile fields",
           "[reference]")
{
    auto dir = makeTempDir();
    auto file = dir.getChildFile ("ref.wav");

    {
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSr, 2, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();

        juce::AudioBuffer<float> buf (2, (int) (8.0 * kSr));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (ch, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                * 200.0 * i / kSr));
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
    }

    const auto p = ReferenceAnalyzer::analyzeFile (file);
    REQUIRE (p.isValid());
    CHECK (p.fromReferenceTrack);
    CHECK (p.name == "ref");
    // A -12 dBFS sine lands in a plausible loudness region (not -100, not 0)
    CHECK (p.targetLufs > -40.0);
    CHECK (p.targetLufs < -3.0);

    // Unreadable file → invalid profile
    CHECK_FALSE (ReferenceAnalyzer::analyzeFile (juce::File ("/nonexistent/r.wav")).isValid());

    dir.deleteRecursively();
}

TEST_CASE ("SessionPlanner: reference profile nudges gain staging (REF-01/02), never the master",
           "[reference][planner]")
{
    juce::Array<StemInfo> stems;
    stems.add (stem ("/t/Bass.wav",   "Bass",   "Bass",   -20.0));
    stems.add (stem ("/t/Vocals.wav", "Vocals", "Vocals", -20.0));

    const auto hipHop  = ReferenceProfile::genre ("Hip-Hop / Rap");
    const auto podcast = ReferenceProfile::genre ("Podcast");

    const auto neutral = SessionPlanner::plan (stems);
    const auto bassy   = SessionPlanner::plan (stems, &hipHop);
    const auto vocal   = SessionPlanner::plan (stems, &podcast);

    // Hip-Hop pushes the bass hotter than neutral; Podcast pushes vocals.
    CHECK (bassy.tracks[0].gainDb > neutral.tracks[0].gainDb);
    CHECK (vocal.tracks[1].gainDb > neutral.tracks[1].gainDb);
    // …and Podcast pulls the bass DOWN relative to neutral (lowDb is negative)
    CHECK (vocal.tracks[0].gainDb < neutral.tracks[0].gainDb);

    // Offsets are clamped — even a crazy profile can't exceed ±3 dB per group
    ReferenceProfile extreme;
    extreme.name = "Extreme";
    extreme.lowDb = 30.0;
    CHECK (SessionPlanner::groupOffsetDb ("Bass", extreme) <= 3.0);

    // REF-04: an invalid/absent profile changes nothing
    ReferenceProfile invalid;
    CHECK (SessionPlanner::groupOffsetDb ("Bass", invalid) == 0.0);
}
