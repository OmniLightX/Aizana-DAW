// test_ai_agent_bridge.cpp — Phase 4 flagship "Aizana AI Studio": the non-UI core that
// lets an in-app chat drive the user's Claude Code / Codex CLI. Pure pieces are golden-tested
// here; the subprocess + UI (AiStudioPanel) are user-tested.

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "ai/AiAgentBridge.h"

using namespace aizana;

namespace
{
    juce::File makeTempDir (const juce::String& name)
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
        d.deleteRecursively();
        d.createDirectory();
        return d;
    }
}

TEST_CASE ("AiAgentBridge: detectCli prefers claude, falls back to codex", "[ai][aistudio]")
{
    auto dir = makeTempDir ("aizana_cli_test");

    SECTION ("none present")
    {
        CHECK (AiAgentBridge::detectCli ({ dir.getFullPathName() }).type == AiCli::None);
    }

    SECTION ("codex only")
    {
        dir.getChildFile ("codex").create();
        const auto info = AiAgentBridge::detectCli ({ dir.getFullPathName() });
        CHECK (info.type == AiCli::Codex);
        CHECK (info.executable.getFileName() == "codex");
    }

    SECTION ("claude wins over codex")
    {
        dir.getChildFile ("codex").create();
        dir.getChildFile ("claude").create();
        const auto info = AiAgentBridge::detectCli ({ dir.getFullPathName() });
        CHECK (info.type == AiCli::ClaudeCode);
        CHECK (info.displayName == "Claude Code");
    }

    dir.deleteRecursively();
}

TEST_CASE ("AiAgentBridge: detectClis lists all installed CLIs, Claude first", "[ai][aistudio]")
{
    auto dir = makeTempDir ("aizana_clis_test");
    dir.getChildFile ("codex").create();
    dir.getChildFile ("claude").create();

    const auto all = AiAgentBridge::detectClis ({ dir.getFullPathName() });
    REQUIRE (all.size() == 2);
    CHECK (all.getReference (0).type == AiCli::ClaudeCode);   // preferred first
    CHECK (all.getReference (1).type == AiCli::Codex);

    dir.deleteRecursively();
}

TEST_CASE ("AiAgentBridge: buildCodexArgs runs `exec` and registers the MCP server via -c", "[ai][aistudio]")
{
    const juce::File exe ("/opt/homebrew/bin/codex");
    const juce::File bin ("/tmp/aizana-mcp");
    const juce::File tc  ("/tmp/GeneratorToolchain");

    const auto args = AiAgentBridge::buildCodexArgs (exe, "make a reverb", bin, tc);
    CHECK (args[0] == exe.getFullPathName());
    CHECK (args.contains ("exec"));
    // Finder-launched DAWs do not run inside a source checkout.
    CHECK (args.contains ("--skip-git-repo-check"));
    CHECK (args.contains ("--approve-for-me"));
    CHECK (args.contains ("--cd"));
    CHECK (juce::File (args[args.indexOf ("--cd") + 1]).getFileName() == "workspace");
    CHECK (args.contains ("make a reverb"));
    bool hasServerOverride = false;
    for (const auto& a : args)
        if (a.contains ("mcp_servers.aizana.command") && a.contains (bin.getFullPathName()))
            hasServerOverride = true;
    CHECK (hasServerOverride);
}

TEST_CASE ("AiAgentBridge: writeClaudeMcpConfig registers the aizana server", "[ai][aistudio]")
{
    auto dir = makeTempDir ("aizana_mcpcfg_test");
    const auto cfg  = dir.getChildFile ("mcp.json");
    const auto bin  = dir.getChildFile ("aizana-mcp");
    const auto tool = dir.getChildFile ("GeneratorToolchain");

    AiAgentBridge::writeClaudeMcpConfig (cfg, bin, tool);
    REQUIRE (cfg.existsAsFile());

    const auto v = juce::JSON::parse (cfg);
    const auto server = v.getProperty ("mcpServers", {}).getProperty ("aizana", {});
    CHECK (server.getProperty ("command", {}).toString() == bin.getFullPathName());
    CHECK (server.getProperty ("env", {}).getProperty ("AIZANA_TOOLCHAIN_DIR", {}).toString()
             == tool.getFullPathName());

    dir.deleteRecursively();
}

TEST_CASE ("AiAgentBridge: buildClaudeArgs — session start vs resume + allowed tools", "[ai][aistudio]")
{
    const juce::File exe ("/opt/homebrew/bin/claude");
    const juce::File cfg ("/tmp/mcp.json");

    const auto first = AiAgentBridge::buildClaudeArgs (exe, "make a tape saturator", "sess-123", true, cfg);
    CHECK (first[0] == exe.getFullPathName());
    CHECK (first.contains ("-p"));
    CHECK (first.contains ("make a tape saturator"));
    CHECK (first.contains ("stream-json"));
    CHECK (first.contains ("--mcp-config"));
    CHECK (first.contains (cfg.getFullPathName()));
    CHECK (first.contains ("--session-id"));
    CHECK (first.contains ("sess-123"));
    CHECK_FALSE (first.contains ("--resume"));
    // Aizana MCP tools are pre-approved.
    bool allowed = false;
    for (const auto& a : first) if (a.contains ("mcp__aizana__compile_plugin")) allowed = true;
    CHECK (allowed);

    const auto next = AiAgentBridge::buildClaudeArgs (exe, "louder", "sess-123", false, cfg);
    CHECK (next.contains ("--resume"));
    CHECK (next.contains ("sess-123"));
    CHECK_FALSE (next.contains ("--session-id"));
}

TEST_CASE ("AiAgentBridge: wrapInLoginShell passes argv verbatim", "[ai][aistudio]")
{
    const juce::StringArray argv { "/opt/homebrew/bin/claude", "-p", "hi there" };
    const auto wrapped = AiAgentBridge::wrapInLoginShell (argv);
    CHECK (wrapped[0] == "/bin/zsh");
    CHECK (wrapped[1] == "-lc");
    CHECK (wrapped[2].contains ("exec"));
    CHECK (wrapped[3] == "/opt/homebrew/bin/claude");
    CHECK (wrapped[wrapped.size() - 1] == "hi there");   // not re-quoted/split
}

TEST_CASE ("AiAgentBridge: renderStreamJsonLine extracts assistant text + tool use", "[ai][aistudio]")
{
    // assistant text block
    CHECK (AiAgentBridge::renderStreamJsonLine (
        R"({"type":"assistant","message":{"content":[{"type":"text","text":"Building it now."}]}})")
        == "Building it now.");

    // tool_use → shows the tool name
    CHECK (AiAgentBridge::renderStreamJsonLine (
        R"({"type":"assistant","message":{"content":[{"type":"tool_use","name":"compile_plugin","input":{}}]}})")
        .contains ("compile_plugin"));

    // tool_result (user message) → surfaces the result text
    CHECK (AiAgentBridge::renderStreamJsonLine (
        R"({"type":"user","message":{"content":[{"type":"tool_result","content":[{"type":"text","text":"OK compiled"}]}]}})")
        .contains ("OK compiled"));

    // successful result line → nothing (already streamed)
    CHECK (AiAgentBridge::renderStreamJsonLine (
        R"({"type":"result","subtype":"success","is_error":false,"result":"done"})").isEmpty());

    // error result → surfaced
    CHECK (AiAgentBridge::renderStreamJsonLine (
        R"({"type":"result","is_error":true,"result":"boom"})").contains ("boom"));

    // junk / non-object → empty
    CHECK (AiAgentBridge::renderStreamJsonLine ("not json").isEmpty());
    CHECK (AiAgentBridge::renderStreamJsonLine ("").isEmpty());
}
