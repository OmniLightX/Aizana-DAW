// test_faust_compiler.cpp — Generator G1: prove Cloudes can compile a Faust DSP source
// produced at RUNTIME into a real .clap (the heart of "generate completely new plugins").
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "generator/FaustCompiler.h"

#ifndef CLOUDES_TEST_BUILD_DIR
 #define CLOUDES_TEST_BUILD_DIR "."
#endif
#ifndef CLOUDES_TEST_SOURCE_DIR
 #define CLOUDES_TEST_SOURCE_DIR "."
#endif

using cloudes::FaustCompiler;
using cloudes::FaustToolchain;

TEST_CASE ("FaustCompiler: sanitiseId", "[faustgen]")
{
    REQUIRE (FaustCompiler::sanitiseId ("My Crusher!")   == "my_crusher_");
    REQUIRE (FaustCompiler::sanitiseId ("123 Reverb")    == "reverb");   // strips leading digits/_
    REQUIRE (FaustCompiler::sanitiseId ("")              == "plugin");
}

// Integration: a RUNTIME-authored, non-palette plugin (tremolo) → compiled → loadable .clap.
TEST_CASE ("FaustCompiler: compiles a runtime DSP into a real .clap", "[faustgen]")
{
    auto tc = FaustToolchain::resolve();   // faust + archdir + faustInclude from the install
    // Dev toolchain headers (the app bundles these; tests use the fetched/source copies):
    tc.clapInclude        = juce::File (CLOUDES_TEST_BUILD_DIR).getChildFile ("_deps/clap_gen-src/include");
    tc.clapHelpersInclude = juce::File (CLOUDES_TEST_BUILD_DIR).getChildFile ("_deps/clap_helpers-src/include");
    tc.guiGlobals         = juce::File (CLOUDES_TEST_SOURCE_DIR).getChildFile ("generator/faust_gui_globals.cpp");

    if (! tc.faust.existsAsFile() || ! tc.clapInclude.isDirectory() || ! tc.guiGlobals.existsAsFile())
        SKIP ("Faust toolchain not available in this environment");

    // A plugin that is NOT in any palette — generated on the fly.
    const juce::String dsp =
        "declare name \"RuntimeTremolo\";\n"
        "import(\"stdfaust.lib\");\n"
        "rate  = hslider(\"Rate[unit:Hz]\", 5, 0.1, 20, 0.01);\n"
        "depth = hslider(\"Depth\", 0.5, 0, 1, 0.01);\n"
        "lfo   = (1 - depth) + depth * 0.5 * (1 + os.osc(rate));\n"
        "process = par(i, 2, _ * lfo);\n";

    const auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("cloudes_faustgen_test");
    outDir.deleteRecursively();

    const auto r = FaustCompiler::compile (dsp, "Runtime Tremolo", tc, outDir);
    INFO ("compiler log:\n" << r.log);
    REQUIRE (r.ok);
    REQUIRE (r.clapFile.existsAsFile());
    REQUIRE (r.clapFile.getSize() > 1000);          // a real dylib, not an empty stub
    REQUIRE (r.clapFile.getFileName() == "runtime_tremolo.clap");

    outDir.deleteRecursively();
}
