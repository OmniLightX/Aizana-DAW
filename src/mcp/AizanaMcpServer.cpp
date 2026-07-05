// AizanaMcpServer — Phase 4 / Generator G4: a standalone MCP (Model Context Protocol)
// stdio server that lets Claude Code / Codex (or any MCP client) author brand-new audio
// plugins for Aizana from free text.
//
// Design: lokal-first. The local composer (G2) stays the default in-app author; this
// server is the OPT-IN path where an LLM writes arbitrary Faust DSP. It reuses the exact
// same C++ generator code (FaustCompiler/FaustEmitter/LocalComposer) — single source of
// truth, no logic duplicated in another language. Compiled plugins land in the user's
// Aizana library (~/Library/Aizana/Generated Plugins); a running
// Aizana instance auto-discovers them (PluginScanner::registerGeneratedPlugins + the
// browser's 1 Hz poll), so "describe -> plugin appears in the DAW" works end to end.
//
// Transport: newline-delimited JSON-RPC 2.0 over stdio (the MCP stdio transport).
// stdout carries protocol messages ONLY; diagnostics go to stderr.
//
// Tools exposed:
//   - faust_reference : the Faust idioms Aizana supports (read this first to emit valid DSP)
//   - compose_plugin  : local baseline DSP for a prompt (a starting point to refine)
//   - compile_plugin  : compile authored Faust DSP -> real .clap in the user's library
//   - list_plugins    : list already-generated plugins

#include <juce_core/juce_core.h>

#include "generator/FaustCompiler.h"
#include "generator/FaustEmitter.h"
#include "generator/LocalComposer.h"
#include "generator/PluginSpec.h"
#include "ai/AiLiveProtocol.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace aizana;

namespace
{
    constexpr const char* kServerName      = "aizana";
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
            "Aizana plugin authoring — Faust quick reference\n"
            "================================================\n"
            "You write a complete, self-contained Faust DSP program; Aizana compiles it to a\n"
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
            "your final DSP. On success the plugin is in the user's Aizana library and shows up\n"
            "in the plugin browser under the \"Aizana\" manufacturer.");
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
                "Install Faust (`brew install faust`) and make sure Aizana.app is installed in "
                "/Applications, or set the AIZANA_TOOLCHAIN_DIR environment variable to a "
                "GeneratorToolchain directory (clap/clap-helpers headers + faust_gui_globals.cpp).",
                true);

        const auto r = FaustCompiler::compile (dsp, name);   // resolve toolchain + write to library

        if (r.ok)
        {
            juce::String out;
            out << "OK — compiled \"" << name << "\".\n"
                << r.clapFile.getFullPathName() << "\n\n"
                << "The plugin is now in the user's Aizana library. A running Aizana picks it up "
                   "automatically (plugin browser, \"Aizana\" group). Tell the user to insert it "
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
                out << files.size() << " generated plugin(s) in the Aizana library:\n";
                for (const auto& f : files)
                    out << "  - " << f.getFileNameWithoutExtension()
                        << "   (" << f.getFullPathName() << ")\n";
            }
        }
        return textResult (id, out);
    }

    //==============================================================================
    // Live mixing tools (Slice 2) — proxy to the RUNNING Aizana session.
    //
    // Unlike the generator tools (which write .clap files to disk), these steer the live
    // Edit: aizana-mcp connects to the app's loopback bridge (discovery file → port+token)
    // and forwards one AiLiveProtocol request. The app applies the move via AICommandBridge
    // (one undo step) and replies. If Aizana isn't running, we return a clear, actionable
    // message rather than failing silently.
    //==============================================================================

    juce::String callLiveBridge (const juce::String& tool, const juce::var& args, bool& ok)
    {
        ok = false;

        const auto disco = AiLiveProtocol::readDiscovery (AiLiveProtocol::liveBridgeFile());
        if (! disco.valid)
            return "Aizana is not running, or its live bridge is off. Ask the user to open "
                   "Aizana and click the \"KI-Studio\" button, then try again.";

        juce::StreamingSocket sock;
        if (! sock.connect ("127.0.0.1", disco.port, 3000))
            return "could not reach the running Aizana session (loopback port "
                   + juce::String (disco.port) + ").";

        const auto reqLine = AiLiveProtocol::makeRequest (disco.token, tool, args) + "\n";
        const auto utf8 = reqLine.toRawUTF8();
        const int  len  = (int) std::strlen (utf8);
        if (sock.write (utf8, len) != len)
            return "failed to send the request to Aizana.";

        std::string buf;
        while (buf.size() < (1u << 20))
        {
            if (sock.waitUntilReady (true, 6000) != 1)
                break;

            char tmp[1024];
            const int n = sock.read (tmp, (int) sizeof tmp, false);
            if (n <= 0)
                break;

            buf.append (tmp, (size_t) n);
            const auto nl = buf.find ('\n');
            if (nl != std::string::npos) { buf.resize (nl); break; }
        }

        const auto resp = AiLiveProtocol::parseResponse (
            juce::String::fromUTF8 (buf.c_str(), (int) buf.size()));

        if (! resp.valid) return "got an unreadable reply from Aizana.";
        if (! resp.ok)    return resp.error;

        ok = true;
        return resp.text;
    }

    juce::var toolLive (const juce::var& id, const juce::String& tool, const juce::var& args)
    {
        bool ok = false;
        const auto text = callLiveBridge (tool, args, ok);
        return textResult (id, ok ? text : juce::String ("error: " + text), ! ok);
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
            "Return the Faust DSP idioms Aizana supports. Call this FIRST before writing a "
            "plugin so the DSP you author compiles cleanly.",
            R"({"type":"object","properties":{},"additionalProperties":false})"));

        tools.add (tool ("compose_plugin",
            "Generate a local baseline Faust DSP for a free-text prompt using Aizana' built-in "
            "composer. Use it as a starting point to refine, or compile it as-is.",
            R"({"type":"object","properties":{"prompt":{"type":"string","description":"Free-text description of the desired plugin, e.g. 'warm tape saturation with a touch of reverb'."}},"required":["prompt"],"additionalProperties":false})"));

        tools.add (tool ("compile_plugin",
            "Compile a complete Faust DSP program into a real CLAP plugin and add it to the user's "
            "Aizana library. `process` must be a 2-in/2-out stereo effect. Returns the .clap path "
            "or the compiler error log.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"Display name of the plugin, e.g. 'Warm Tape'."},"faust_dsp":{"type":"string","description":"The complete self-contained Faust program (declare name + import(\"stdfaust.lib\") + process)."}},"required":["name","faust_dsp"],"additionalProperties":false})"));

        tools.add (tool ("list_plugins",
            "List the plugins already generated into the user's Aizana library.",
            R"({"type":"object","properties":{},"additionalProperties":false})"));

        // --- Live mixing tools (act on the RUNNING Aizana session) ---------------
        tools.add (tool ("list_tracks",
            "List the audio tracks in the user's CURRENTLY OPEN Aizana session, with each "
            "track's name, volume (dB) and pan. Call this FIRST before changing the mix so you "
            "use exact track names. Requires Aizana to be running with the KI-Studio open.",
            R"({"type":"object","properties":{},"additionalProperties":false})"));

        tools.add (tool ("set_track_volume",
            "Set the volume (in dB) of one track in the live Aizana session. Applies "
            "immediately as a single undo step. Use list_tracks first for the exact track name.",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name, as returned by list_tracks."},"db":{"type":"number","description":"Target volume in dB (e.g. -3.0). Clamped to [-60, +12]."}},"required":["track","db"],"additionalProperties":false})"));

        tools.add (tool ("set_track_pan",
            "Set the stereo pan of one track in the live Aizana session. Applies immediately "
            "as a single undo step. Use list_tracks first for the exact track name.",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name, as returned by list_tracks."},"pan":{"type":"number","description":"Pan position: -1.0 = hard left, 0 = centre, +1.0 = hard right."}},"required":["track","pan"],"additionalProperties":false})"));

        tools.add (tool ("mute_track",
            "Mute or un-mute one track in the live Aizana session (one undo step).",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"muted":{"type":"boolean","description":"true = mute, false = un-mute."}},"required":["track","muted"],"additionalProperties":false})"));

        tools.add (tool ("solo_track",
            "Solo or un-solo one track in the live Aizana session (one undo step).",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"soloed":{"type":"boolean","description":"true = solo, false = un-solo."}},"required":["track","soloed"],"additionalProperties":false})"));

        tools.add (tool ("set_track_eq",
            "Add a parametric EQ to one track in the live Aizana session (one EQ instance per "
            "call, up to 8 bands). Each band: type (high_pass | low_shelf | high_shelf | bell), "
            "freq in Hz, gain in dB (ignored for high_pass), optional q. One undo step.",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"bands":{"type":"array","items":{"type":"object","properties":{"type":{"type":"string","enum":["high_pass","low_shelf","high_shelf","bell"]},"freq":{"type":"number","description":"Centre/corner frequency in Hz."},"gain":{"type":"number","description":"Gain in dB (bell/shelf). Ignored for high_pass."},"q":{"type":"number","description":"Bandwidth (default 0.7)."}},"required":["type","freq"]},"description":"1-8 EQ bands."}},"required":["track","bands"],"additionalProperties":false})"));

        tools.add (tool ("add_compressor",
            "Insert a built-in compressor on one track in the live Aizana session (one undo "
            "step). Sensible defaults if a parameter is omitted (ratio 2:1, threshold -18 dB).",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"threshold_db":{"type":"number","description":"Threshold in dB (default -18)."},"ratio":{"type":"number","description":"Compression ratio, e.g. 2 for 2:1 (default 2)."},"makeup_db":{"type":"number","description":"Make-up gain in dB (default 0)."}},"required":["track"],"additionalProperties":false})"));

        tools.add (tool ("add_deesser",
            "Insert a built-in de-esser on one track (tames sibilance) in the live Aizana "
            "session (one undo step). Defaults to ~5.5 kHz if omitted.",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"freq_hz":{"type":"number","description":"Sibilance frequency in Hz (default 5500)."},"threshold_db":{"type":"number","description":"Threshold in dB (default -20)."},"ratio":{"type":"number","description":"Ratio (default 4)."}},"required":["track"],"additionalProperties":false})"));

        tools.add (tool ("add_reverb",
            "Add reverb to one track in the live Aizana session the way pros do it: a send to a "
            "shared \"Reverb\" FX bus (created once with a 100%-wet reverb), not a per-track "
            "insert. Call again on other tracks to share the same space. One undo step.",
            R"({"type":"object","properties":{"track":{"type":"string","description":"Exact track name."},"amount":{"type":"number","description":"How much reverb: 0 = none .. 1 = full send level (default 0.3)."}},"required":["track"],"additionalProperties":false})"));

        tools.add (tool ("create_bus_channel",
            "Create a named bus channel in the live Aizana session (or reuse an existing bus of "
            "that name) and optionally route tracks to it. The bus is a regular mixer channel: "
            "afterwards you can EQ/compress it directly with set_track_eq / add_compressor using "
            "the bus name (e.g. drum bus glue). Call again with the same name to route more "
            "tracks. One undo step.",
            R"({"type":"object","properties":{"name":{"type":"string","description":"Bus display name, e.g. 'Drum Bus'."},"tracks":{"type":"array","items":{"type":"string"},"description":"Optional: exact track names to route to this bus (list_tracks for names)."}},"required":["name"],"additionalProperties":false})"));

        return makeResult (id, makeObject ({ { "tools", juce::var (tools) } }));
    }

    juce::var handleToolCall (const juce::var& id, const juce::var& params)
    {
        const auto name = params.getProperty ("name", {}).toString();
        const auto args = params.getProperty ("arguments", juce::var());

        if (name == "faust_reference")  return textResult (id, faustReference());
        if (name == "compose_plugin")   return toolComposePlugin (id, args);
        if (name == "compile_plugin")   return toolCompilePlugin (id, args);
        if (name == "list_plugins")     return toolListPlugins (id);

        // Live mixing — proxy to the running Aizana session.
        if (name == "list_tracks")      return toolLive (id, "list_tracks", args);
        if (name == "set_track_volume") return toolLive (id, "set_track_volume", args);
        if (name == "set_track_pan")    return toolLive (id, "set_track_pan", args);
        if (name == "mute_track")       return toolLive (id, "mute_track", args);
        if (name == "solo_track")       return toolLive (id, "solo_track", args);
        if (name == "set_track_eq")     return toolLive (id, "set_track_eq", args);
        if (name == "add_compressor")   return toolLive (id, "add_compressor", args);
        if (name == "add_deesser")      return toolLive (id, "add_deesser", args);
        if (name == "add_reverb")       return toolLive (id, "add_reverb", args);
        if (name == "create_bus_channel") return toolLive (id, "create_bus_channel", args);

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
