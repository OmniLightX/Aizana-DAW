// test_faust_compiler.cpp — Generator G1: prove Aizana can compile a Faust DSP source
// produced at RUNTIME into a real .clap (the heart of "generate completely new plugins").
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <cmath>

#include "generator/FaustCompiler.h"
#include "../src/plugins/ClapPluginFormat.h"
#include "../src/plugins/PluginScanner.h"

#ifndef AIZANA_TEST_BUILD_DIR
 #define AIZANA_TEST_BUILD_DIR "."
#endif
#ifndef AIZANA_TEST_SOURCE_DIR
 #define AIZANA_TEST_SOURCE_DIR "."
#endif

using aizana::FaustCompiler;
using aizana::FaustToolchain;

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
    tc.clapInclude        = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_gen-src/include");
    tc.clapHelpersInclude = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_helpers-src/include");
    tc.guiGlobals         = juce::File (AIZANA_TEST_SOURCE_DIR).getChildFile ("generator/faust_gui_globals.cpp");

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
                            .getChildFile ("aizana_faustgen_test");
    outDir.deleteRecursively();

    const auto r = FaustCompiler::compile (dsp, "Runtime Tremolo", tc, outDir);
    INFO ("compiler log:\n" << r.log);
    REQUIRE (r.ok);
    REQUIRE (r.clapFile.existsAsFile());
    REQUIRE (r.clapFile.getSize() > 1000);          // a real dylib, not an empty stub
    REQUIRE (r.clapFile.getFileName() == "runtime_tremolo.clap");

    outDir.deleteRecursively();
}

// Integration: a generated .clap, hosted via ClapPluginFormat, must respond to host
// parameter changes — proves the Param-GUI is functional, not just cosmetic. Without a
// working ClapParameter::setValue the generic editor's sliders would be dead.
TEST_CASE ("FaustCompiler: host a generated .clap and prove param setValue changes audio", "[faustgen][plugins]")
{
    auto tc = FaustToolchain::resolve();
    tc.clapInclude        = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_gen-src/include");
    tc.clapHelpersInclude = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_helpers-src/include");
    tc.guiGlobals         = juce::File (AIZANA_TEST_SOURCE_DIR).getChildFile ("generator/faust_gui_globals.cpp");

    if (! tc.faust.existsAsFile() || ! tc.clapInclude.isDirectory() || ! tc.guiGlobals.existsAsFile())
        SKIP ("Faust toolchain not available in this environment");

    // Gain plugin: out = in * Gain. Default 0 → silence; set to 1 → unity passthrough.
    // Raw hslider (no si.smoo) so the value applies immediately within the block.
    const juce::String dsp =
        "declare name \"RuntimeGain\";\n"
        "import(\"stdfaust.lib\");\n"
        "g = hslider(\"Gain\", 0, 0, 1, 0.001);\n"
        "process = par(i, 2, _ * g);\n";

    const auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("aizana_faustgen_param_test");
    outDir.deleteRecursively();

    const auto r = FaustCompiler::compile (dsp, "Runtime Gain", tc, outDir);
    INFO ("compiler log:\n" << r.log);
    REQUIRE (r.ok);
    REQUIRE (r.clapFile.existsAsFile());

    // Host the freshly compiled plugin.
    ClapPluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, r.clapFile.getFullPathName());
    REQUIRE (descs.size() >= 1);

    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String err;
    fmt.createPluginInstance (*descs[0], 44100.0, 512,
        [&] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& e)
        { instance = std::move (inst); err = e; });

    REQUIRE (instance != nullptr);
    REQUIRE (err.isEmpty());

    instance->prepareToPlay (44100.0, 512);

    // Fill both channels with a DC level, process, return the peak magnitude.
    auto processDC = [&] (float level) -> float
    {
        juce::AudioBuffer<float> buf (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buf.getWritePointer (ch), level, 512);
        juce::MidiBuffer midi;
        instance->processBlock (buf, midi);
        return buf.getMagnitude (0, 512);
    };

    // Default Gain = 0 → output silent.
    processDC (1.0f);
    CHECK (processDC (1.0f) < 0.01f);

    // Find the "Gain" parameter and drive it to maximum.
    juce::AudioProcessorParameter* gain = nullptr;
    for (auto* p : instance->getParameters())
        if (p->getName (64).containsIgnoreCase ("Gain")) { gain = p; break; }
    REQUIRE (gain != nullptr);

    gain->setValueNotifyingHost (1.0f);

    // setValue enqueues; the change is applied on the next processBlock (FIFO drain).
    float outFull = 0.0f;
    for (int i = 0; i < 4; ++i)
        outFull = processDC (1.0f);

    CHECK (outFull > 0.9f);                                   // audio actually changed
    CHECK (std::abs (gain->getValue() - 1.0f) < 1.0e-4f);     // host value is source of truth

    instance->releaseResources();
    outDir.deleteRecursively();
}

// G5 Library: a generated .clap registered into the KnownPluginList so it appears in the
// plugin browser / insert menu like any other plugin and survives restarts.
TEST_CASE ("Generated library: registerGeneratedPlugins adds a generated .clap to the KnownPluginList", "[faustgen][plugins][library]")
{
    auto tc = FaustToolchain::resolve();
    tc.clapInclude        = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_gen-src/include");
    tc.clapHelpersInclude = juce::File (AIZANA_TEST_BUILD_DIR).getChildFile ("_deps/clap_helpers-src/include");
    tc.guiGlobals         = juce::File (AIZANA_TEST_SOURCE_DIR).getChildFile ("generator/faust_gui_globals.cpp");

    if (! tc.faust.existsAsFile() || ! tc.clapInclude.isDirectory() || ! tc.guiGlobals.existsAsFile())
        SKIP ("Faust toolchain not available in this environment");

    const juce::String dsp =
        "declare name \"LibraryGain\";\n"
        "import(\"stdfaust.lib\");\n"
        "g = hslider(\"Gain\", 1, 0, 1, 0.001);\n"
        "process = par(i, 2, _ * g);\n";

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("aizana_genlib_test");
    dir.deleteRecursively();
    dir.createDirectory();

    const auto r = FaustCompiler::compile (dsp, "Library Gain", tc, dir);
    INFO ("compiler log:\n" << r.log);
    REQUIRE (r.ok);
    REQUIRE (r.clapFile.existsAsFile());

    PluginScanner scanner;
    const int before = scanner.getKnownPlugins().getTypes().size();

    const int added = scanner.registerGeneratedPlugins (dir);
    CHECK (added >= 1);
    CHECK (scanner.getKnownPlugins().getTypes().size() == before + added);

    // The new entry is a CLAP plugin grouped under the "Aizana" manufacturer.
    bool found = false;
    for (const auto& t : scanner.getKnownPlugins().getTypes())
        if (t.fileOrIdentifier == r.clapFile.getFullPathName())
        {
            found = true;
            CHECK (t.pluginFormatName == "CLAP");
            CHECK (t.manufacturerName == "Aizana");
        }
    CHECK (found);

    // Idempotent: re-registering the same directory adds nothing (de-duped by identifier).
    CHECK (scanner.registerGeneratedPlugins (dir) == 0);

    // G5b — rename: the override shows in the live list and survives a re-register.
    REQUIRE (scanner.renameGeneratedPlugin (r.clapFile, "My Renamed Gain"));
    auto nameInList = [&] (const juce::String& path) -> juce::String
    {
        for (const auto& t : scanner.getKnownPlugins().getTypes())
            if (t.fileOrIdentifier == path)
                return t.name;
        return {};
    };
    CHECK (nameInList (r.clapFile.getFullPathName()) == "My Renamed Gain");
    CHECK (PluginScanner::generatedDisplayName (r.clapFile) == "My Renamed Gain");

    // A fresh scanner picks up the override from the sidecar (persists across restarts).
    {
        PluginScanner reopened;
        reopened.registerGeneratedPlugins (dir);
        bool foundRenamed = false;
        for (const auto& t : reopened.getKnownPlugins().getTypes())
            if (t.fileOrIdentifier == r.clapFile.getFullPathName() && t.name == "My Renamed Gain")
                foundRenamed = true;
        CHECK (foundRenamed);
    }

    // Blank name is rejected.
    CHECK_FALSE (scanner.renameGeneratedPlugin (r.clapFile, "   "));

    // G5b — delete: removed from the list and the file is gone.
    const int countBeforeDelete = scanner.getKnownPlugins().getTypes().size();
    REQUIRE (scanner.deleteGeneratedPlugin (r.clapFile));
    CHECK_FALSE (r.clapFile.existsAsFile());
    CHECK (scanner.getKnownPlugins().getTypes().size() < countBeforeDelete);
    CHECK (nameInList (r.clapFile.getFullPathName()).isEmpty());

    dir.deleteRecursively();
}
