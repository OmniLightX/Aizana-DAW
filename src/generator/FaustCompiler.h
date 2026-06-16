#pragma once

#include <juce_core/juce_core.h>

namespace cloudes
{
    /**
        FaustCompiler — Phase 4 / Generator G1: turn a Faust DSP *source string* generated
        at RUNTIME into a real, loadable .clap plugin. This is the heart of the true plugin
        generator: the local composer (G2) or an opt-in LLM (G4) writes Faust DSP, and this
        compiles it into a brand-new plugin — not a fixed palette.

        Compiles by shelling out to the system `faust` + `clang` (the recipe proven in
        Spike S2): faust -a clap-arch.cpp <dsp> → C++ → clang -shared → universal .clap.
        Synchronous + does file I/O and spawns processes → call ONLY on a background thread,
        never the audio or message thread.

        Toolchain pieces (clap / clap-helpers headers, faust_gui_globals.cpp) ship in the app
        bundle (Resources/GeneratorToolchain); `faust` itself must be installed on the machine
        (G4/release can bundle libfaust). isAvailable() reports whether generation is possible.
    */
    struct FaustToolchain
    {
        juce::File   faust;                // faust executable
        juce::String archdir;              // `faust --archdir` (holds clap/clap-arch.cpp etc.)
        juce::File   faustInclude;         // Faust C++ runtime headers (<prefix>/include)
        juce::File   clapInclude;          // CLAP C-API include dir
        juce::File   clapHelpersInclude;   // clap-helpers include dir
        juce::File   guiGlobals;           // faust_gui_globals.cpp (one-time GUI statics)

        bool isComplete() const;

        /** Resolves the toolchain: bundled (app Resources/GeneratorToolchain) first, then a
            dev fallback (build/_deps + generator/). `faust`/archdir come from the install. */
        static FaustToolchain resolve();
    };

    struct FaustCompileResult
    {
        bool         ok = false;
        juce::File   clapFile;
        juce::String log;     // compiler output on failure (for diagnostics)
    };

    class FaustCompiler
    {
    public:
        /** True when a usable Faust toolchain is present (generation possible). */
        static bool isAvailable();

        /** ~/Library/Application Support/Cloudes/Generated Plugins — the user's plugin library. */
        static juce::File generatedPluginsDir();

        /** Compile a Faust DSP source string → universal .clap at `outDir/<name>.clap`.
            Explicit toolchain (testable). Background thread only. */
        static FaustCompileResult compile (const juce::String& faustDspSource,
                                           const juce::String& pluginName,
                                           const FaustToolchain& tc,
                                           const juce::File& outDir);

        /** Convenience: resolve() toolchain + write into generatedPluginsDir(). */
        static FaustCompileResult compile (const juce::String& faustDspSource,
                                           const juce::String& pluginName);

        /** File-system-safe lower id from a display name ("My Crusher!" → "my_crusher"). */
        static juce::String sanitiseId (const juce::String& name);
    };
}
