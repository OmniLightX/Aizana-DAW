#pragma once

#include <juce_core/juce_core.h>

namespace aizana
{
    /** Which external AI CLI backs the in-app assistant. */
    enum class AiCli { None, ClaudeCode, Codex };

    struct AiCliInfo
    {
        AiCli        type = AiCli::None;
        juce::File   executable;
        juce::String displayName;   // "Claude Code" / "Codex"

        bool isValid() const { return type != AiCli::None && executable.existsAsFile(); }
    };

    /**
        AiAgentBridge — Phase 4 flagship ("Aizana AI Studio"): the non-UI core that lets
        an in-app chat drive the user's own Claude Code / Codex CLI as an agent. The CLI is
        the brain; Aizana exposes its tools (plugin generation, and later live mixing) to it
        via the bundled `aizana-mcp` server. Local-first: uses the user's existing CLI login,
        no API key, no logic in the cloud beyond what their assistant already does.

        Pure + deterministic where it can be (config/args/parse are golden-testable). The
        actual subprocess + chat UI live in AiStudioPanel; spawning, threads and streaming
        are kept out of here so this layer stays unit-testable.
    */
    class AiAgentBridge
    {
    public:
        /** Locate an installed agent CLI (Claude Code preferred, then Codex) via `which`. */
        static AiCliInfo detectCli();

        /** Testable variant: search the given directories for `claude` then `codex`. */
        static AiCliInfo detectCli (const juce::StringArray& candidateDirs);

        /** All installed agent CLIs (for a backend chooser), Claude Code first. */
        static juce::Array<AiCliInfo> detectClis();
        static juce::Array<AiCliInfo> detectClis (const juce::StringArray& candidateDirs);

        /** Write an MCP config JSON registering the bundled `aizana-mcp` (stdio) server so
            the spawned CLI can call Aizana' plugin tools. Returns the file written. */
        static juce::File writeClaudeMcpConfig (const juce::File& configFile,
                                                const juce::File& aizanaMcpBinary,
                                                const juce::File& toolchainDir);

        /** Build the Claude Code argv for one chat turn (without the login-shell wrapper).
            firstTurn → start a session (`--session-id`); otherwise resume it (`--resume`). */
        static juce::StringArray buildClaudeArgs (const juce::File& claudeExe,
                                                  const juce::String& prompt,
                                                  const juce::String& sessionId,
                                                  bool firstTurn,
                                                  const juce::File& mcpConfig);

        /** Private working directory for Codex launched from the GUI. */
        static juce::File codexWorkspace();

        /** Build the Codex `exec` argv for one turn, registering the aizana MCP server via
            -c config overrides (no global config change). Codex prints plain text → stream raw
            (no stream-json parsing). */
        static juce::StringArray buildCodexArgs (const juce::File& codexExe,
                                                 const juce::String& prompt,
                                                 const juce::File& aizanaMcpBinary,
                                                 const juce::File& toolchainDir);

        /** Wrap an argv in a login shell so the child inherits the user's PATH (node etc.)
            while still passing our args verbatim (no shell re-quoting → injection-safe):
            /bin/zsh -lc 'exec "$0" "$@"' <exe> <args...>. */
        static juce::StringArray wrapInLoginShell (const juce::StringArray& argv);

        /** Parse one line of Claude Code `--output-format stream-json` into displayable
            transcript text (assistant text + tool-use notices). Empty → show nothing. */
        static juce::String renderStreamJsonLine (const juce::String& line);
    };
}
