// test_ai_live_protocol.cpp — Phase 4 flagship "Aizana AI Studio", Slice 2 (live mixing).
// Golden tests for the pure wire-format seam shared by the running app (AiStudioLiveServer)
// and the spawned aizana-mcp. The socket round-trip + TE mutation are user-tested.

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "ai/AiLiveProtocol.h"

using namespace aizana;

namespace
{
    juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& p : props) o->setProperty (p.first, p.second);
        return juce::var (o);
    }
}

TEST_CASE ("AiLiveProtocol: request round-trips through JSON", "[aistudio][live]")
{
    const auto args = obj ({ { "track", "Lead Vocal" }, { "db", -3.5 } });
    const auto line = AiLiveProtocol::makeRequest ("tok-123", "set_track_volume", args);

    // One physical line — the newline-delimited transport depends on it.
    CHECK_FALSE (line.containsChar ('\n'));

    const auto r = AiLiveProtocol::parseRequest (line);
    REQUIRE (r.valid);
    CHECK (r.token == "tok-123");
    CHECK (r.tool  == "set_track_volume");
    CHECK (r.args.getProperty ("track", {}).toString() == "Lead Vocal");
    CHECK (juce::approximatelyEqual ((double) r.args.getProperty ("db", 0.0), -3.5));
}

TEST_CASE ("AiLiveProtocol: missing tool / malformed line is invalid", "[aistudio][live]")
{
    CHECK_FALSE (AiLiveProtocol::parseRequest ("not json at all").valid);
    CHECK_FALSE (AiLiveProtocol::parseRequest ("{}").valid);                       // no tool
    CHECK_FALSE (AiLiveProtocol::parseRequest (R"({"token":"x"})").valid);         // no tool
    CHECK       (AiLiveProtocol::parseRequest (R"({"tool":"list_tracks"})").valid);
}

TEST_CASE ("AiLiveProtocol: nested array args (EQ bands) survive the round-trip", "[aistudio][live]")
{
    juce::Array<juce::var> bands;
    bands.add (obj ({ { "type", "high_pass" }, { "freq", 80.0 } }));
    bands.add (obj ({ { "type", "bell" }, { "freq", 3000.0 }, { "gain", -2.5 }, { "q", 1.2 } }));
    const auto args = obj ({ { "track", "Vox" }, { "bands", juce::var (bands) } });

    const auto r = AiLiveProtocol::parseRequest (
        AiLiveProtocol::makeRequest ("t", "set_track_eq", args));
    REQUIRE (r.valid);

    auto* arr = r.args.getProperty ("bands", {}).getArray();
    REQUIRE (arr != nullptr);
    REQUIRE (arr->size() == 2);
    CHECK ((*arr)[0].getProperty ("type", {}).toString() == "high_pass");
    CHECK (juce::approximatelyEqual ((double) (*arr)[1].getProperty ("gain", 0.0), -2.5));
    CHECK (juce::approximatelyEqual ((double) (*arr)[1].getProperty ("q", 0.0), 1.2));
}

TEST_CASE ("AiLiveProtocol: empty args default to an object", "[aistudio][live]")
{
    const auto line = AiLiveProtocol::makeRequest ("t", "list_tracks", juce::var());
    const auto r    = AiLiveProtocol::parseRequest (line);
    REQUIRE (r.valid);
    CHECK (r.args.isObject());
}

TEST_CASE ("AiLiveProtocol: ok response carries text", "[aistudio][live]")
{
    const auto line = AiLiveProtocol::makeResponseOk ("Set \"Drums\" to -2.0 dB.");
    CHECK_FALSE (line.containsChar ('\n'));

    const auto r = AiLiveProtocol::parseResponse (line);
    REQUIRE (r.valid);
    CHECK (r.ok);
    CHECK (r.text == "Set \"Drums\" to -2.0 dB.");
    CHECK (r.error.isEmpty());
}

TEST_CASE ("AiLiveProtocol: error response carries error", "[aistudio][live]")
{
    const auto r = AiLiveProtocol::parseResponse (AiLiveProtocol::makeResponseError ("no such track"));
    REQUIRE (r.valid);
    CHECK_FALSE (r.ok);
    CHECK (r.error == "no such track");
}

TEST_CASE ("AiLiveProtocol: malformed response is invalid", "[aistudio][live]")
{
    CHECK_FALSE (AiLiveProtocol::parseResponse ("garbage").valid);
}

TEST_CASE ("AiLiveProtocol: discovery file round-trips", "[aistudio][live]")
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("aizana_live_test").getChildFile ("live.json");
    file.deleteFile();

    SECTION ("missing file → invalid")
    {
        CHECK_FALSE (AiLiveProtocol::readDiscovery (file).valid);
    }

    SECTION ("write then read")
    {
        AiLiveProtocol::writeDiscovery (file, 49711, "secret-token");
        const auto d = AiLiveProtocol::readDiscovery (file);
        REQUIRE (d.valid);
        CHECK (d.port  == 49711);
        CHECK (d.token == "secret-token");
    }

    SECTION ("port 0 or empty token → invalid")
    {
        AiLiveProtocol::writeDiscovery (file, 0, "tok");
        CHECK_FALSE (AiLiveProtocol::readDiscovery (file).valid);

        AiLiveProtocol::writeDiscovery (file, 49711, "");
        CHECK_FALSE (AiLiveProtocol::readDiscovery (file).valid);
    }

    file.getParentDirectory().deleteRecursively();
}

TEST_CASE ("AiLiveProtocol: bridge file lives under Aizana/aistudio", "[aistudio][live]")
{
    const auto f = AiLiveProtocol::liveBridgeFile();
    CHECK (f.getFileName() == "live.json");
    CHECK (f.getParentDirectory().getFileName() == "aistudio");
    CHECK (f.getFullPathName().contains ("Aizana"));
}

TEST_CASE ("AiLiveProtocol: discoveryMatches guards ownership of live.json", "[aistudio][live]")
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("aizana_live_match_test").getChildFile ("live.json");
    file.deleteFile();

    // Missing file is owned by nobody — stop() must NOT delete, timer MUST rewrite.
    CHECK_FALSE (AiLiveProtocol::discoveryMatches (file, 49720, "tok-A"));

    AiLiveProtocol::writeDiscovery (file, 49720, "tok-A");
    CHECK      (AiLiveProtocol::discoveryMatches (file, 49720, "tok-A"));   // ours
    CHECK_FALSE (AiLiveProtocol::discoveryMatches (file, 49720, "tok-B"));  // another instance re-minted a token
    CHECK_FALSE (AiLiveProtocol::discoveryMatches (file, 49721, "tok-A"));  // another instance re-bound a port

    // A second instance clobbers the file → we no longer own it (stop() leaves it alone).
    AiLiveProtocol::writeDiscovery (file, 49721, "tok-B");
    CHECK_FALSE (AiLiveProtocol::discoveryMatches (file, 49720, "tok-A"));
    CHECK      (AiLiveProtocol::discoveryMatches (file, 49721, "tok-B"));

    file.getParentDirectory().deleteRecursively();
}
