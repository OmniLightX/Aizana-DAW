#include "AiAgentBridge.h"

namespace cloudes
{
    namespace
    {
        juce::String whichTool (const juce::String& name)
        {
            juce::ChildProcess proc;
            if (proc.start (juce::StringArray { "/usr/bin/which", name }))
            {
                const auto out = proc.readAllProcessOutput().trim();
                if (juce::File::isAbsolutePath (out) && juce::File (out).existsAsFile())
                    return out;
            }
            return {};
        }

        juce::StringArray commonCliDirs()
        {
            juce::StringArray dirs;
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

            // The GUI app (launched from Finder) does NOT inherit the shell PATH, so probe
            // the usual install locations explicitly in addition to whatever PATH we do have.
            if (const auto path = juce::SystemStats::getEnvironmentVariable ("PATH", {}); path.isNotEmpty())
                dirs.addArray (juce::StringArray::fromTokens (path, ":", {}));

            dirs.add ("/opt/homebrew/bin");
            dirs.add ("/usr/local/bin");
            dirs.add (home.getChildFile (".local/bin").getFullPathName());
            dirs.add (home.getChildFile (".claude/local").getFullPathName());   // claude native installer
            dirs.add (home.getChildFile (".bun/bin").getFullPathName());
            dirs.add (home.getChildFile (".npm-global/bin").getFullPathName());
            dirs.add ("/usr/bin");
            dirs.removeEmptyStrings();
            dirs.removeDuplicates (false);
            return dirs;
        }
    }

    AiCliInfo AiAgentBridge::detectCli()
    {
        // `which` first (respects a real PATH if we have one), then the common dirs.
        if (const auto c = whichTool ("claude"); c.isNotEmpty())
            return { AiCli::ClaudeCode, juce::File (c), "Claude Code" };
        if (const auto c = whichTool ("codex"); c.isNotEmpty())
            return { AiCli::Codex, juce::File (c), "Codex" };

        return detectCli (commonCliDirs());
    }

    AiCliInfo AiAgentBridge::detectCli (const juce::StringArray& candidateDirs)
    {
        for (const auto& d : candidateDirs)
        {
            const auto exe = juce::File (d).getChildFile ("claude");
            if (exe.existsAsFile())
                return { AiCli::ClaudeCode, exe, "Claude Code" };
        }
        for (const auto& d : candidateDirs)
        {
            const auto exe = juce::File (d).getChildFile ("codex");
            if (exe.existsAsFile())
                return { AiCli::Codex, exe, "Codex" };
        }
        return {};
    }

    juce::File AiAgentBridge::writeClaudeMcpConfig (const juce::File& configFile,
                                                   const juce::File& cloudesMcpBinary,
                                                   const juce::File& toolchainDir)
    {
        auto* env = new juce::DynamicObject();
        if (toolchainDir != juce::File())
            env->setProperty ("CLOUDES_TOOLCHAIN_DIR", toolchainDir.getFullPathName());

        auto* server = new juce::DynamicObject();
        server->setProperty ("command", cloudesMcpBinary.getFullPathName());
        server->setProperty ("args", juce::var (juce::Array<juce::var>{}));
        server->setProperty ("env", juce::var (env));

        auto* servers = new juce::DynamicObject();
        servers->setProperty ("cloudes", juce::var (server));

        auto* root = new juce::DynamicObject();
        root->setProperty ("mcpServers", juce::var (servers));

        configFile.getParentDirectory().createDirectory();
        configFile.replaceWithText (juce::JSON::toString (juce::var (root)));
        return configFile;
    }

    juce::StringArray AiAgentBridge::buildClaudeArgs (const juce::File& claudeExe,
                                                     const juce::String& prompt,
                                                     const juce::String& sessionId,
                                                     bool firstTurn,
                                                     const juce::File& mcpConfig)
    {
        juce::StringArray args;
        args.add (claudeExe.getFullPathName());
        args.add ("-p");
        args.add (prompt);
        args.add ("--output-format"); args.add ("stream-json");
        args.add ("--verbose");
        args.add ("--mcp-config");    args.add (mcpConfig.getFullPathName());
        // Pre-approve the Cloudes MCP tools so the agent can act without interactive prompts.
        args.add ("--allowedTools");
        args.add ("mcp__cloudes__faust_reference,mcp__cloudes__compose_plugin,"
                  "mcp__cloudes__compile_plugin,mcp__cloudes__list_plugins");

        if (firstTurn)
        {
            args.add ("--session-id");
            args.add (sessionId);
        }
        else
        {
            args.add ("--resume");
            args.add (sessionId);
        }
        return args;
    }

    juce::StringArray AiAgentBridge::wrapInLoginShell (const juce::StringArray& argv)
    {
        // exec "$0" "$@" → run argv[0] with the remaining args, no shell re-parsing of our
        // values (injection-safe), while -l loads the user's PATH so node/claude resolve.
        juce::StringArray out { "/bin/zsh", "-lc", "exec \"$0\" \"$@\"" };
        out.addArray (argv);
        return out;
    }

    juce::String AiAgentBridge::renderStreamJsonLine (const juce::String& line)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty())
            return {};

        const auto v = juce::JSON::parse (trimmed);
        if (! v.isObject())
            return {};

        const auto type = v.getProperty ("type", {}).toString();

        // Assistant turn: text blocks + a short notice for each tool call.
        if (type == "assistant")
        {
            juce::String out;
            const auto message = v.getProperty ("message", {});
            const auto content = message.getProperty ("content", {});
            if (auto* arr = content.getArray())
                for (const auto& block : *arr)
                {
                    const auto bt = block.getProperty ("type", {}).toString();
                    if (bt == "text")
                        out << block.getProperty ("text", {}).toString();
                    else if (bt == "tool_use")
                        out << "\n\xf0\x9f\x94\xa7 " << block.getProperty ("name", {}).toString() << "\xe2\x80\xa6\n";
                }
            return out;
        }

        // Tool results come back as a "user" message — surface the text (e.g. the compile log).
        if (type == "user")
        {
            juce::String out;
            const auto message = v.getProperty ("message", {});
            const auto content = message.getProperty ("content", {});
            if (auto* arr = content.getArray())
                for (const auto& block : *arr)
                    if (block.getProperty ("type", {}).toString() == "tool_result")
                    {
                        const auto inner = block.getProperty ("content", {});
                        if (inner.isString())
                            out << inner.toString();
                        else if (auto* ia = inner.getArray())
                            for (const auto& ib : *ia)
                                if (ib.getProperty ("type", {}).toString() == "text")
                                    out << ib.getProperty ("text", {}).toString();
                    }
            return out.isNotEmpty() ? ("\n" + out + "\n") : juce::String();
        }

        // Final result line: text was already streamed via assistant events; only surface errors.
        if (type == "result")
        {
            const bool isError = (bool) v.getProperty ("is_error", false);
            if (isError)
                return "\n\xe2\x9a\xa0\xef\xb8\x8f " + v.getProperty ("result", {}).toString() + "\n";
            return {};
        }

        return {};   // system/init and unknown events: nothing to show
    }
}
