#pragma once

#include "PluginSpec.h"

namespace cloudes
{
    /**
        FaustEmitter — Phase 4 / Generator G2: PluginSpec → Faust DSP source string.

        Pure and deterministic (no I/O, no globals) → golden-testable. The output is a
        complete, self-contained Faust program ready for FaustCompiler::compile (G1):

            declare name "…"; import("stdfaust.lib");
            stage0 = <2-in/2-out expr> with { <sliders> };
            …
            process = vgroup("0 Name", stage0) : vgroup("1 Name", stage1) : … ;

        Every stage is a stereo transformer, so the chain is plain serial composition.
        Per-stage `vgroup`s keep slider UI paths (→ CLAP param ids) unique even when two
        stages share a label, while each stage's `with {}` gives short local var names.
    */
    class FaustEmitter
    {
    public:
        /** Render a full Faust program for `spec`. Empty chain → stereo passthrough. */
        static juce::String emit (const PluginSpec& spec);
    };
}
