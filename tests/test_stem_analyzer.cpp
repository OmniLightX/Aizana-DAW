// test_stem_analyzer.cpp — Phase 3, Iteration 1, Slice 1 (AI-01):
// two-tier stem classification (filename heuristics + audio-feature fallback)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ai/StemAnalyzer.h"

namespace
{
    constexpr double kSr = 44100.0;

    juce::File makeTempDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("cloudes_stems_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        REQUIRE (dir.createDirectory());
        return dir;
    }

    juce::File writeWav (const juce::File& dir, const juce::String& name,
                         const juce::AudioBuffer<float>& buf)
    {
        auto file = dir.getChildFile (name);
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        REQUIRE (stream != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), kSr, 1, 16, {}, 0));
        REQUIRE (writer != nullptr);
        stream.release();
        REQUIRE (writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()));
        return file;
    }

    /** Click track: noise bursts every beat — drum-like (high onset rate, bright). */
    juce::AudioBuffer<float> makeClicks (double seconds)
    {
        juce::AudioBuffer<float> buf (1, (int) (seconds * kSr));
        buf.clear();
        juce::Random rng (7);
        const double period = 60.0 / 120.0 * kSr;
        const int clickLen = (int) (0.02 * kSr);
        for (double pos = 0.0; pos + clickLen < buf.getNumSamples(); pos += period)
            for (int i = 0; i < clickLen; ++i)
                buf.addSample (0, (int) pos + i,
                               (rng.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) i / clickLen) * 0.8f);
        return buf;
    }

    /** Sustained low sine — bass-like (low centroid, no onsets). */
    juce::AudioBuffer<float> makeLowSine (double seconds, double freq, float amp)
    {
        juce::AudioBuffer<float> buf (1, (int) (seconds * kSr));
        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * freq * i / kSr));
        return buf;
    }
}

TEST_CASE ("StemAnalyzer Tier 1: real-world stem names classify correctly", "[stems]")
{
    struct Case { const char* name; const char* group; };
    const Case cases[] =
    {
        { "Kick In",            "Drums"   },
        { "kick2",              "Drums"   },
        { "SNARE_TOP",          "Drums"   },
        { "Overheads L",        "Drums"   },
        { "Schlagzeug Raum",    "Drums"   },
        { "808 Sub",            "Bass"    },
        { "Bassline final",     "Bass"    },
        { "Gtr DI",             "Guitars" },
        { "Gitarre Rhythmus",   "Guitars" },
        { "Vocal Mic",          "Vocals"  },
        { "LeadVox_comp",       "Vocals"  },
        { "Gesang Hook",        "Vocals"  },
        { "BGV Stack",          "Vocals"  },
        { "Piano Verse",        "Keys"    },
        { "SynthPad wide",      "Keys"    },
        { "Strings Outro",      "Other"   },
        { "Riser 8bar",         "FX"      },
    };

    for (const auto& c : cases)
    {
        juce::String instr, group;
        double conf = 0.0;
        INFO ("name: " << c.name);
        REQUIRE (StemAnalyzer::classifyFromName (c.name, instr, group, conf));
        CHECK (group == juce::String (c.group));
        CHECK (conf > 0.4);
    }
}

TEST_CASE ("StemAnalyzer Tier 1: token matching avoids false positives", "[stems]")
{
    juce::String instr, group;
    double conf = 0.0;

    // "brass" must NOT match the "bass" keyword (token-based) — it matches Brass.
    REQUIRE (StemAnalyzer::classifyFromName ("Brass Section", instr, group, conf));
    CHECK (group == "Other");
    CHECK (instr == "Brass");

    // Names with no instrument hint at all → no Tier-1 result
    CHECK_FALSE (StemAnalyzer::classifyFromName ("Track 7", instr, group, conf));
    CHECK_FALSE (StemAnalyzer::classifyFromName ("LINE 6", instr, group, conf));
    CHECK_FALSE (StemAnalyzer::classifyFromName ("final_v3_MASTER", instr, group, conf));
}

TEST_CASE ("StemAnalyzer Tier 2: audio fallback separates drums / bass / other", "[stems]")
{
    juce::String instr, group;
    double conf = 0.0;

    auto clicks = makeClicks (10.0);
    StemAnalyzer::classifyFromAudio (clicks.getReadPointer (0), clicks.getNumSamples(), kSr,
                                     instr, group, conf);
    CHECK (group == "Drums");
    CHECK (conf >= 0.5);

    auto bass = makeLowSine (10.0, 80.0, 0.5f);
    StemAnalyzer::classifyFromAudio (bass.getReadPointer (0), bass.getNumSamples(), kSr,
                                     instr, group, conf);
    CHECK (group == "Bass");
    CHECK (conf >= 0.5);

    // Mid-range sustained tone: conservative "Other", low confidence
    auto mid = makeLowSine (10.0, 1000.0, 0.5f);
    StemAnalyzer::classifyFromAudio (mid.getReadPointer (0), mid.getNumSamples(), kSr,
                                     instr, group, conf);
    CHECK (group == "Other");
    CHECK (conf < 0.5);
}

TEST_CASE ("StemAnalyzer: analyzeFile combines tiers and measures levels", "[stems]")
{
    auto dir = makeTempDir();

    // Named file → Tier 1 wins regardless of content
    auto kickFile = writeWav (dir, "Kick In.wav", makeClicks (5.0));
    auto kick = StemAnalyzer::analyzeFile (kickFile);
    CHECK (kick.fromFilename);
    CHECK (kick.group == "Drums");
    CHECK (kick.peakDb > -20.0);
    CHECK (kick.rmsDb  > -60.0);
    CHECK (kick.rmsDb  < kick.peakDb);

    // Anonymous file → Tier 2 (low sine = bass)
    auto anonFile = writeWav (dir, "Track 7.wav", makeLowSine (5.0, 80.0, 0.5f));
    auto anon = StemAnalyzer::analyzeFile (anonFile);
    CHECK_FALSE (anon.fromFilename);
    CHECK (anon.group == "Bass");
    REQUIRE_THAT (anon.peakDb, Catch::Matchers::WithinAbs (-6.0, 1.0));

    // Unreadable file → safe defaults, no crash
    auto missing = StemAnalyzer::analyzeFile (juce::File ("/nonexistent/whatever.wav"));
    CHECK (missing.group == "Other");
    CHECK (missing.confidence == 0.0);

    dir.deleteRecursively();
}
