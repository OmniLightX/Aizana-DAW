#pragma once

#include "PluginSpec.h"

namespace aizana
{
    /**
        LocalComposer — Phase 4 / Generator G2: the default, *local* plugin author (no LLM).

        Turns a DE/EN free-text request ("warmer Tape-Delay mit leichter Sättigung",
        "punchy bus glue + air") into a PluginSpec by recognising intents and assembling
        blocks in a sensible mixing order (cleanup → tone → drive → dynamics → modulation
        → time). Pure and deterministic → golden-testable.

        MCP-ready: this is the built-in proposer behind the same Text→Spec seam an opt-in
        LLM (G4) plugs into; the spec it produces feeds FaustEmitter → FaustCompiler.
    */
    class LocalComposer
    {
    public:
        /** Compose a PluginSpec from a free-text prompt. Never empty: an unrecognised
            prompt yields a neutral Gain utility so the result is always loadable. */
        static PluginSpec compose (const juce::String& prompt);
    };
}
