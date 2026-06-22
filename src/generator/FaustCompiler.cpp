#include "FaustCompiler.h"

namespace cloudes
{
    namespace
    {
        // Run a process (no shell → paths with spaces are safe via StringArray). Returns
        // exit code; appends combined output to `log`. -1 if it couldn't start.
        int run (const juce::StringArray& args, juce::String& log)
        {
            juce::ChildProcess proc;
            if (! proc.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            {
                log << "\n[!] could not start: " << args.joinIntoString (" ");
                return -1;
            }
            log << proc.readAllProcessOutput();      // blocks until the process exits
            proc.waitForProcessToFinish (120000);
            return proc.getExitCode();
        }

        juce::File firstExisting (const juce::StringArray& paths)
        {
            for (const auto& p : paths)
                if (juce::File::isAbsolutePath (p) && juce::File (p).existsAsFile())
                    return juce::File (p);
            return {};
        }
    }

    //==============================================================================
    bool FaustToolchain::isComplete() const
    {
        return faust.existsAsFile() && archdir.isNotEmpty()
            && faustInclude.isDirectory() && clapInclude.isDirectory()
            && clapHelpersInclude.isDirectory() && guiGlobals.existsAsFile();
    }

    FaustToolchain FaustToolchain::resolve()
    {
        FaustToolchain tc;

        tc.faust = firstExisting ({ "/opt/homebrew/bin/faust", "/usr/local/bin/faust",
                                    "/opt/local/bin/faust" });
        if (tc.faust == juce::File())
        {
            // last resort: ask the shell
            juce::String out;
            juce::ChildProcess which;
            if (which.start (juce::StringArray { "/usr/bin/which", "faust" }))
            {
                out = which.readAllProcessOutput().trim();
                if (juce::File::isAbsolutePath (out))
                    tc.faust = juce::File (out);
            }
        }

        if (tc.faust.existsAsFile())
        {
            juce::String log;
            juce::ChildProcess proc;
            if (proc.start (juce::StringArray { tc.faust.getFullPathName(), "--archdir" }))
                tc.archdir = proc.readAllProcessOutput().trim();

            // <prefix>/include holds the Faust C++ runtime headers (faust/dsp/dsp.h …)
            tc.faustInclude = tc.faust.getParentDirectory().getParentDirectory().getChildFile ("include");
        }

        // clap / clap-helpers headers + GUI globals: bundled with Cloudes. Resolution order:
        //   1. CLOUDES_TOOLCHAIN_DIR env — explicit override (used by the standalone MCP server)
        //   2. this executable's own .app bundle — the Cloudes app case
        //   3. a GeneratorToolchain dir next to the executable — the standalone cloudes-mcp case
        //   4. an installed Cloudes.app — so a relocated cloudes-mcp still finds the headers
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        juce::StringArray candidates;
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("CLOUDES_TOOLCHAIN_DIR", {});
            env.isNotEmpty())
            candidates.add (env);
        candidates.add (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                            .getChildFile ("Contents/Resources/GeneratorToolchain").getFullPathName());
        candidates.add (exe.getParentDirectory().getChildFile ("GeneratorToolchain").getFullPathName());
        candidates.add ("/Applications/Cloudes.app/Contents/Resources/GeneratorToolchain");
        candidates.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                            .getChildFile ("Applications/Cloudes.app/Contents/Resources/GeneratorToolchain")
                            .getFullPathName());

        for (const auto& c : candidates)
        {
            const juce::File dir (c);
            if (dir.getChildFile ("faust_gui_globals.cpp").existsAsFile())
            {
                tc.clapInclude        = dir.getChildFile ("clap/include");
                tc.clapHelpersInclude = dir.getChildFile ("clap-helpers/include");
                tc.guiGlobals         = dir.getChildFile ("faust_gui_globals.cpp");
                break;
            }
        }

        return tc;
    }

    //==============================================================================
    bool FaustCompiler::isAvailable() { return FaustToolchain::resolve().isComplete(); }

    juce::File FaustCompiler::generatedPluginsDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Cloudes/Generated Plugins");
    }

    juce::String FaustCompiler::sanitiseId (const juce::String& name)
    {
        juce::String s;
        for (auto c : name.toLowerCase())
            s << (juce::CharacterFunctions::isLetterOrDigit (c) ? juce::String::charToString (c)
                                                                : juce::String ("_"));
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789_").trimCharactersAtStart ("_0123456789");
        return s.isNotEmpty() ? s : juce::String ("plugin");
    }

    FaustCompileResult FaustCompiler::compile (const juce::String& faustDspSource,
                                               const juce::String& pluginName,
                                               const FaustToolchain& tc,
                                               const juce::File& outDir)
    {
        FaustCompileResult r;

        if (! tc.isComplete())
        {
            r.log = "Faust toolchain incomplete (is `faust` installed? are the bundled CLAP "
                    "headers present?).";
            return r;
        }

        const auto id   = sanitiseId (pluginName);
        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("cloudes_gen_" + id + "_"
                                             + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        work.createDirectory();
        outDir.createDirectory();

        const auto safeName = pluginName.isNotEmpty() ? pluginName : juce::String ("Generated Plugin");
        work.getChildFile ("plugin_metadata.h").replaceWithText (
            "#define FAUST_PLUGIN_ID \"com.cloudes.gen." + id + "\"\n"
            "#define FAUST_PLUGIN_NAME \"" + safeName + "\"\n"
            "#define FAUST_PLUGIN_VENDOR \"Cloudes\"\n"
            "#define FAUST_PLUGIN_VERSION \"1.0.0\"\n"
            "#define FAUST_PLUGIN_DESCRIPTION \"Cloudes generated plugin\"\n"
            "#define FAUST_NVOICES 16\n"
            "#define FAUST_IS_POLYPHONIC 0\n");

        const auto dsp    = work.getChildFile (id + ".dsp");
        const auto genCpp = work.getChildFile (id + "_clap.cpp");
        const auto outClap = outDir.getChildFile (id + ".clap");
        dsp.replaceWithText (faustDspSource);

        // 1) Faust DSP + CLAP architecture → standalone C++
        const int fc = run (juce::StringArray {
            tc.faust.getFullPathName(),
            "-a", tc.archdir + "/clap/clap-arch.cpp",
            dsp.getFullPathName(), "-o", genCpp.getFullPathName() }, r.log);

        if (fc != 0 || ! genCpp.existsAsFile())
        {
            r.log = "Faust compile failed.\n" + r.log;
            work.deleteRecursively();
            return r;
        }

        // 2) clang++ → universal, self-contained .clap
        const int cc = run (juce::StringArray {
            "/usr/bin/clang++", "-std=c++17", "-O2", "-fPIC", "-shared",
            "-arch", "arm64", "-arch", "x86_64",
            "-I" + tc.faustInclude.getFullPathName(),
            "-I" + tc.clapInclude.getFullPathName(),
            "-I" + tc.clapHelpersInclude.getFullPathName(),
            "-I" + work.getFullPathName(),
            genCpp.getFullPathName(),
            tc.archdir + "/clap/clap-helpers-impl.cpp",
            tc.guiGlobals.getFullPathName(),
            "-o", outClap.getFullPathName() }, r.log);

        work.deleteRecursively();

        if (cc != 0 || ! outClap.existsAsFile())
        {
            r.log = "clang link failed.\n" + r.log;
            return r;
        }

        r.ok = true;
        r.clapFile = outClap;
        return r;
    }

    FaustCompileResult FaustCompiler::compile (const juce::String& faustDspSource,
                                               const juce::String& pluginName)
    {
        return compile (faustDspSource, pluginName, FaustToolchain::resolve(), generatedPluginsDir());
    }
}
