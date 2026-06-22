// CloudesMcpServer — Phase 4 / Generator G4: a standalone MCP (Model Context Protocol)
// stdio server that lets Claude Code / Codex (or any MCP client) author brand-new audio
// plugins for Cloudes from free text.
//
// Design: lokal-first. The local composer (G2) stays the default in-app author; this
// server is the OPT-IN path where an LLM writes arbitrary Faust DSP. It reuses the exact
// same C++ generator code (FaustCompiler/FaustEmitter/LocalComposer) — single source of
// truth, no logic duplicated in another language. Compiled plugins land in the user's
// Cloudes library (~/Library/Application Support/Cloudes/Generated Plugins); a running
// Cloudes instance auto-discovers them (PluginScanner::registerGeneratedPlugins + the
// browser's 1 Hz poll), so "describe -> plugin appears in the DAW" works end to end.
//
// Transport: newline-delimited JSON-RPC 2.0 over stdio (the MCP stdio transport).
// stdout carries protocol messages ONLY; diagnostics go to stderr.
//
// Tools exposed:
//   - faust_reference : the Faust idioms Cloudes supports (read this first to emit valid DSP)
//   - compose_plugin  : local baseline DSP for a prompt (a starting point to refine)
//   - compile_plugin  : compile authored Faust DSP -> real .clap in the user's library
//   - list_plugins    : list already-generated plugins

#include <juce_core/juce_core.h>

#include "generator/FaustCompiler.h"
#include "generator/FaustEmitter.h"
#include "generator/LocalComposer.h"
#include "generator/PluginSpec.h"

#include <iostream>
#include <string>

using namespace cloudes;

namespace
{
    constexpr const char* kServerName      = "cloudes";
    constexpr const char* kServerVersion   = "0.1.0";
    constexpr const char* kProtocolVersion = "2024-11-05";

    //==============================================================================
    // JSON-RPC plumbing
    //==============================================================================

    void writeMessage (const juce::var& msg)
    {
        // Compact (single line) — string values keep embedded newlines escaped as \n,
        // so each message is exactly one physical line as the stdio transport requires.
        std::cout << juce::JSON::toString (msg, true).toStdString() << '\n';
        std::cout.flush();
    }

    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto* o = new juce::DynamicObject();
        for (const auto& p : props)
            o->setProperty (p.first, p.second);
        return juce::var (o);
    }

    juce::var makeResult (const juce::var& id, const juce::var& result)
    {
        return makeObject ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } });
    }

    juce::var makeError (const juce::var& id, int code, const juce::String& message)
    {
        return makeObject ({ { "jsonrpc", "2.0" }, { "id", id },
                             { "error", makeObject ({ { "code", code }, { "message", message } }) } });
    }

    // MCP tool result: a content array with a single text block.
    juce::var textResult (const juce::var& id, const juce::String& text, bool isError = false)
    {
        juce::Array<juce::var> content;
        content.add (makeObject ({ { "type", "text" }, { "text", text } }));

        auto* o = new juce::DynamicObject();
        o->setProperty ("content", juce::var (content));
        if (isError)
            o->setProperty ("isError", true);
        return makeResult (id, juce::var (o));
    }

    //==============================================================================
    // Tool implementations
    //==============================================================================

    juce::String faustReference()
    {
        return juce::String::fromUTF8 (
            "Cloudes plugin authoring — Faust quick reference\n"
            "================================================\n"
            "You write a complete, self-contained Faust DSP program; Cloudes compiles it to a\n"
            "universal CLAP plugin. Rules:\n"
            "\n"
            "1. Start with:   declare name \"My Plugin\";   import(\"stdfaust.lib\");\n"
            "2. `process` MUST be a STEREO effect: 2 inputs -> 2 outputs.\n"
            "   - Mono effect applied to both channels:  process = par(i, 2, <mono-expr>);\n"
            "   - Native stereo effect (e.g. reverb):    process = <stereo-expr>;\n"
            "3. Every user knob is an hslider — these become the plugin's parameters:\n"
            "     hslider(\"Drive\", 2, 1, 10, 0.01)            // label, default, min, max, step\n"
            "     hslider(\"Cutoff[unit:Hz][scale:log]\", 1000, 20, 20000, 1)\n"
            "   Give knobs unique labels; metadata [unit:..] / [scale:log] is optional.\n"
            "4. No external files, no soundfiles, no GUI code. Audio only.\n"
            "\n"
            "Useful stdfaust building blocks:\n"
            "  Gain         *(ba.db2linear(g))           with g = hslider(\"Gain[unit:dB]\",0,-24,24,0.1)\n"
            "  Lowpass      fi.lowpass(2, fc)            Highpass  fi.highpass(2, fc)\n"
            "  Peak EQ      fi.peak_eq_cq(gainDb, f, q)  Shelves   fi.lowshelf(N,g,f) fi.highshelf(N,g,f)\n"
            "  Saturation   ef.cubicnl(drive, 0)         Soft clip ma.tanh(x*d)/ma.tanh(d)\n"
            "  Compressor   co.compressor_stereo(ratio, threshDb, attSec, relSec)\n"
            "  Delay        de.fdelay(ma.SR, ba.sec2samp(timeSec))   Echo  ef.echo(maxSec, timeSec, fb)\n"
            "  Reverb       re.stereo_freeverb(size, size, damp, 0.5)\n"
            "  LFO / osc    os.osc(rateHz)               Pitch     ef.transpose(1024, 256, semitones)\n"
            "  Dry/wet      x*(1-mix) + fx(x)*mix\n"
            "\n"
            "Example — warm tape-ish saturator with tone + output trim:\n"
            "  declare name \"Warm Tape\"; import(\"stdfaust.lib\");\n"
            "  drive = hslider(\"Drive\", 3, 1, 12, 0.01);\n"
            "  tone  = hslider(\"Tone[unit:Hz][scale:log]\", 8000, 1000, 18000, 1);\n"
            "  out   = hslider(\"Output[unit:dB]\", 0, -24, 6, 0.1);\n"
            "  process = par(i, 2, ma.tanh(_ * drive)/ma.tanh(drive) : fi.lowpass(2, tone) : *(ba.db2linear(out)));\n"
            "\n"
            "Workflow: optionally call compose_plugin for a baseline, then compile_plugin with\n"
            "your final DSP. On success the plugin is in the user's Cloudes library and shows up\n"
            "in the plugin browser under the \"Cloudes\" manufacturer.");
    }

    juce::var toolComposePlugin (const juce::var& id, const juce::var& args)
    {
        const auto prompt = args.getProperty ("prompt", {}).toString();
        if (prompt.isEmpty())
            return textResult (id, "error: 'prompt' (string) is required.", true);

        const auto spec = LocalComposer::compose (prompt);
        const auto dsp  = FaustEmitter::emit (spec);

        juce::String out;
        out << "// Local baseline for prompt: " << prompt << "\n"
            << "// Plugin name: " << spec.name << "\n"
            << "// Refine this DSP (or write your own) and pass it to compile_plugin.\n\n"
            << dsp;
        return textResult (id, out);
    }

    juce::var toolCompilePlugin (const juce::var& id, const juce::var& args)
    {
        const auto name = args.getProperty ("name", {}).toString();
        const auto dsp  = args.getProperty ("faust_dsp", {}).toString();

        if (name.isEmpty() || dsp.isEmpty())
            return textResult (id, "error: both 'name' (string) and 'faust_dsp' (string) are required.", true);

        if (! FaustCompiler::isAvailable())
            return textResult (id,
                "error: the Faust toolchain is not available.\n"
                "Install Faust (`brew install faust`) and make sure Cloudes.app is installed in "
                "/Applications, or set the CLOUDES_TOOLCHAIN_DIR environment variable to a "
                "GeneratorToolchain directory (clap/clap-helpers headers + faust_gui_globals.cpp).",
                true);

        const auto r = FaustCompiler::compile (dsp, name);   // resolve toolchain + write to library

        if (r.ok)
        {
            juce::String out;
            out << "OK — compiled \"" << name << "\".\n"
                << r.clapFile.getFullPathName() << "\n\n"
                << "The plugin is now in the user's Cloudes library. A running Cloudes picks it up "
                   "automatically (plugin browser, \"Cloudes\" group). Tell the user to insert it "
                   "from the browser onto a track.";
            return textResult (id, out);
        }

        juce::String out;
        out << "FAILED to compile \"" << name << "\".\n\n"
            << "Compiler output:\n" << r.log << "\n\n"
            << "Fix the Faust source and try again. Call faust_reference for the supported idioms "
               "and remember `process` must be a 2-in/2-out stereo effect.";
        return textResult (id, out, true);
    }

    juce::var toolListPlugins (const juce::var& id)
    {
        const auto dir = FaustCompiler::generatedPluginsDir();

        juce::String out;
        if (! dir.isDirectory())
        {
            out = "(no plugins generated yet)";
        }
        else
        {
            const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.clap");
            if (files.isEmpty())
            {
                out = "(no plugins generated yet)";
            }
            else
            {
                out << files.size() << " generated plugin(s) in the Cloudes library:\n";
                for (const auto& f : files)
                    out << "  - " << f.getFileNameWithoutExtension()
                        << "   (" << f.getFullPathName() << ")\n";
            }
        }
        return textResult (id, out);
    }

    juce::var toolsListResult (const juce::var& id)
    {
        const auto tool = [] (const char* name, const char* description, const char* schemaJson)
        {
            return makeObject ({ { "name", name },
                                 { "description", description },
                                 { "inputSchema", juce::JSON::parse (schemaJson) } });
        };

        juce::Array<juce::var> tools;

        tools.add (tool ("faust_reference",
            "Return the Faust DSP idioms Cloudes supports. Call this FIRST before writing a "
            "plugin so the DSP you author compiles cleanly.",
            R"({"type":"object","properties":{},"additionalProperties":false})"));

        tools.add (tool ("compose_plugin",
            "Generate a local baseline Faust DSP for a free-text prompt using Cloudes' built-in "
            "composer. Use it as a starting point to refine, or compile it as-is.",
            R"({"type":"object","properties":{"prompt":{"type":"string","description":"Free-text description of the desired plugin, e.g. 'warm tape saturation with a touch of reverb'."}},"required":["prompt"],"additionalProperties":false})"));

        tools.add (tool ("compile_plugin",
            "Compile a complete Faust DSP program into a real CLAP plugin and add it to the user's "
            "Cloudes library. `process` must be a 2-in/2-out stereo effect. Returns the .clap path "
            "or the compiler error log.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"Display name of the plugin, e.g. 'Warm Tape'."},"faust_dsp":{"type":"string","description":"The complete self-contained Faust program (declare name + import(\"stdfaust.lib\") + process)."}},"required":["name","faust_dsp"],"additionalProperties":false})"));

        tools.add (tool ("list_plugins",
            "List the plugins already generated into the user's Cloudes library.",
            R"({"type":"object","properties":{},"additionalProperties":false})"));

        return makeResult (id, makeObject ({ { "tools", juce::var (tools) } }));
    }

    juce::var handleToolCall (const juce::var& id, const juce::var& params)
    {
        const auto name = params.getProperty ("name", {}).toString();
        const auto args = params.getProperty ("arguments", juce::var());

        if (name == "faust_reference") return textResult (id, faustReference());
        if (name == "compose_plugin")  return toolComposePlugin (id, args);
        if (name == "compile_plugin")  return toolCompilePlugin (id, args);
        if (name == "list_plugins")    return toolListPlugins (id);

        return makeError (id, -32602, "Unknown tool: " + name);
    }

    juce::var handleInitialize (const juce::var& id, const juce::var& params)
    {
        // Echo the client's requested protocol version if present (else our default).
        auto pv = params.getProperty ("protocolVersion", {}).toString();
        if (pv.isEmpty())
            pv = kProtocolVersion;

        return makeResult (id, makeObject ({
            { "protocolVersion", pv },
            { "capabilities", makeObject ({ { "tools", makeObject ({}) } }) },
            { "serverInfo", makeObject ({ { "name", kServerName }, { "version", kServerVersion } }) }
        }));
    }
}

//==============================================================================
int main()
{
    std::ios::sync_with_stdio (false);

    for (std::string raw; std::getline (std::cin, raw); )
    {
        // Tolerate CRLF and blank keep-alive lines.
        while (! raw.empty() && (raw.back() == '\r' || raw.back() == '\n'))
            raw.pop_back();
        if (raw.empty())
            continue;

        const auto msg = juce::JSON::parse (juce::String (juce::CharPointer_UTF8 (raw.c_str())));
        if (! msg.isObject())
            continue;

        const auto method = msg.getProperty ("method", {}).toString();
        const auto id     = msg.getProperty ("id", juce::var());
        const bool isRequest = msg.hasProperty ("id");   // notifications carry no id

        if (method == "initialize")
        {
            writeMessage (handleInitialize (id, msg.getProperty ("params", juce::var())));
        }
        else if (method == "tools/list")
        {
            writeMessage (toolsListResult (id));
        }
        else if (method == "tools/call")
        {
            writeMessage (handleToolCall (id, msg.getProperty ("params", juce::var())));
        }
        else if (method == "ping")
        {
            writeMessage (makeResult (id, makeObject ({})));
        }
        else if (method.startsWith ("notifications/"))
        {
            // Fire-and-forget (initialized, cancelled, ...) — no response.
        }
        else if (isRequest)
        {
            writeMessage (makeError (id, -32601, "Method not found: " + method));
        }
    }

    return 0;
}
