// test_faust_emitter.cpp — Generator G2: the local composition generator.
// Proves PluginSpec → FaustEmitter is pure/deterministic (golden), that LocalComposer
// turns DE/EN prompts into sensible chains, and that a composed spec actually feeds G1
// (FaustCompiler) into a real .clap — the local, no-LLM path of the true generator.
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "generator/PluginSpec.h"
#include "generator/FaustEmitter.h"
#include "generator/LocalComposer.h"
#include "generator/FaustCompiler.h"

#ifndef CLOUDES_TEST_BUILD_DIR
 #define CLOUDES_TEST_BUILD_DIR "."
#endif
#ifndef CLOUDES_TEST_SOURCE_DIR
 #define CLOUDES_TEST_SOURCE_DIR "."
#endif

using namespace cloudes;

TEST_CASE ("FaustEmitter: empty chain → stereo passthrough", "[faustgen][g2]")
{
    PluginSpec spec;
    spec.name = "Empty";
    const auto src = FaustEmitter::emit (spec);

    REQUIRE (src.contains ("declare name \"Empty\";"));
    REQUIRE (src.contains ("process = _, _;"));
}

TEST_CASE ("FaustEmitter: deterministic golden output", "[faustgen][g2]")
{
    PluginSpec spec;
    spec.name = "Test Chain";
    spec.chain = { Blocks::lowpass (1000.0f), Blocks::driveCubic (0.5f) };

    const juce::String expected =
        "declare name \"Test Chain\";\n"
        "declare author \"Cloudes\";\n"
        "import(\"stdfaust.lib\");\n"
        "\n"
        "stage0 = par(i, 2, fi.lowpass(2, fc)) with {\n"
        "    fc = hslider(\"Cutoff[unit:Hz][scale:log]\", 1000, 20, 20000, 1);\n"
        "};\n"
        "\n"
        "stage1 = par(i, 2, ef.cubicnl(d, 0)) with {\n"
        "    d = hslider(\"Drive\", 0.5, 0, 0.99, 0.01);\n"
        "};\n"
        "\n"
        "process = vgroup(\"0 Lowpass\", stage0) : vgroup(\"1 Saturation\", stage1);\n";

    REQUIRE (FaustEmitter::emit (spec) == expected);
}

TEST_CASE ("FaustEmitter: reverb emits the dry/wet helper", "[faustgen][g2]")
{
    PluginSpec spec;
    spec.chain = { Blocks::reverb() };
    const auto src = FaustEmitter::emit (spec);

    REQUIRE (src.contains ("dw(mix, fx) = "));
    REQUIRE (src.contains ("re.stereo_freeverb(sz, sz, dmp, 0.5)"));
}

TEST_CASE ("FaustEmitter: stereo compressor maps straight through", "[faustgen][g2]")
{
    PluginSpec spec;
    spec.chain = { Blocks::compressor() };
    const auto src = FaustEmitter::emit (spec);

    REQUIRE (src.contains ("co.compressor_stereo(ratio, thr, att, rel)"));
    REQUIRE (src.contains ("att = hslider(\"Attack[unit:ms]\", 10, 0.1, 100, 0.1) / 1000;"));
}

TEST_CASE ("LocalComposer: prompts → sensible chains in mixing order", "[faustgen][g2]")
{
    SECTION ("unrecognised → neutral gain utility")
    {
        const auto spec = LocalComposer::compose ("???");
        REQUIRE (spec.chain.size() == 1);
        REQUIRE (spec.chain[0].kind == BlockKind::Gain);
    }

    SECTION ("warm tape saturation + reverb (DE/EN, tanh variant)")
    {
        const auto spec = LocalComposer::compose ("warmer Tape-Sound mit etwas reverb");
        REQUIRE (spec.chain.size() == 2);
        REQUIRE (spec.chain[0].kind == BlockKind::Drive);
        REQUIRE (spec.chain[0].variant == "tanh");      // warm/tape → tanh
        REQUIRE (spec.chain[1].kind == BlockKind::Reverb);
        REQUIRE (spec.name == "Saturation + Reverb");
    }

    SECTION ("cleanup → tone → dynamics stays in canonical order")
    {
        // low cut → highpass; "air" → high shelf; glue → compressor. Tone before dynamics.
        const auto spec = LocalComposer::compose ("low cut, add air and bus glue compression");
        REQUIRE (spec.chain.size() == 3);
        REQUIRE (spec.chain[0].kind == BlockKind::Filter);
        REQUIRE (spec.chain[0].variant == "highpass");
        REQUIRE (spec.chain[1].kind == BlockKind::Filter);
        REQUIRE (spec.chain[1].variant == "highshelf");
        REQUIRE (spec.chain[2].kind == BlockKind::Compressor);
    }
}

// Integration: composed spec → Faust source → real .clap (G2 feeds G1, no LLM).
TEST_CASE ("LocalComposer → FaustEmitter → FaustCompiler produces a .clap", "[faustgen][g2]")
{
    auto tc = FaustToolchain::resolve();
    tc.clapInclude        = juce::File (CLOUDES_TEST_BUILD_DIR).getChildFile ("_deps/clap_gen-src/include");
    tc.clapHelpersInclude = juce::File (CLOUDES_TEST_BUILD_DIR).getChildFile ("_deps/clap_helpers-src/include");
    tc.guiGlobals         = juce::File (CLOUDES_TEST_SOURCE_DIR).getChildFile ("generator/faust_gui_globals.cpp");

    if (! tc.faust.existsAsFile() || ! tc.clapInclude.isDirectory() || ! tc.guiGlobals.existsAsFile())
        SKIP ("Faust toolchain not available in this environment");

    const auto spec = LocalComposer::compose ("warm tube saturation, glue compression and a little air");
    const auto src  = FaustEmitter::emit (spec);

    const auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("cloudes_g2_test");
    outDir.deleteRecursively();

    const auto r = FaustCompiler::compile (src, spec.name, tc, outDir);
    INFO ("faust source:\n" << src);
    INFO ("compiler log:\n" << r.log);
    REQUIRE (r.ok);
    REQUIRE (r.clapFile.existsAsFile());
    REQUIRE (r.clapFile.getSize() > 1000);

    outDir.deleteRecursively();
}
