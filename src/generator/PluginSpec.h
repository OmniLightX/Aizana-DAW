#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace cloudes
{
    /**
        PluginSpec — Phase 4 / Generator G2: the typed, serialisable vocabulary of DSP
        building blocks the local composer (this slice) or an opt-in LLM (G4) assembles
        into a brand-new plugin. A spec is an ordered chain of blocks; FaustEmitter turns
        it into Faust source, which FaustCompiler (G1) turns into a real .clap.

        Design: every block is a stereo (2-in/2-out) transformer so chaining is just
        serial composition (`a : b : c`). Mono effects (filters, drive) are wrapped with
        `par(i, 2, …)` by the emitter; inherently-stereo effects (compressor, reverb) map
        straight through. Each block carries its own parameters (values + ranges + labels)
        so the whole spec is data — fully serialisable, golden-testable, MCP-ready.
    */
    enum class BlockKind
    {
        Gain,        // utility trim (dB)
        Filter,      // variant: lowpass | highpass | lowshelf | highshelf | peak
        Drive,       // variant: cubic | tanh
        Compressor,  // stereo dynamics
        Tremolo,     // amplitude LFO
        Delay,       // feedback echo
        Reverb       // freeverb + dry/wet
    };

    /** One automatable control → one Faust hslider. */
    struct SpecParam
    {
        juce::String key;       // stable id used by the emitter ("cutoff", "drive", …)
        juce::String label;     // user-facing ("Cutoff") → CLAP param name via Faust path
        juce::String unit;      // "Hz" | "dB" | "ms" | "" — emitted as [unit:…]
        float        value = 0; // current (default) value
        float        min   = 0;
        float        max   = 1;
        float        step   = 0.01f;
        bool         logScale = false; // emitted as [scale:log] (freq controls)
    };

    struct SpecBlock
    {
        BlockKind               kind;
        juce::String            variant;   // sub-type (see BlockKind comments); "" if none
        std::vector<SpecParam>  params;    // ordered; each → one slider

        /** Human-readable stage name used for the Faust group ("Lowpass", "Saturation"). */
        juce::String displayName() const;
    };

    struct PluginSpec
    {
        juce::String           name { "Generated Plugin" };
        std::vector<SpecBlock> chain;

        bool isEmpty() const { return chain.empty(); }
    };

    /** Factory builders — the single source of truth for each block's default params,
        labels and ranges. The composer and tests build chains from these; callers may
        override individual param values afterwards (the spec stays fully serialisable). */
    namespace Blocks
    {
        SpecBlock gain       (float db = 0.0f);
        SpecBlock lowpass    (float cutoffHz = 1000.0f);
        SpecBlock highpass   (float cutoffHz = 100.0f);
        SpecBlock lowShelf   (float freqHz = 200.0f,  float gainDb = 3.0f);
        SpecBlock highShelf  (float freqHz = 4000.0f, float gainDb = 3.0f);
        SpecBlock peak       (float freqHz = 1000.0f, float gainDb = 3.0f, float q = 1.0f);
        SpecBlock driveCubic (float drive = 0.5f);
        SpecBlock driveTanh  (float drive = 3.0f);
        SpecBlock compressor (float ratio = 4.0f, float thresholdDb = -18.0f,
                              float attackMs = 10.0f, float releaseMs = 100.0f,
                              float makeupDb = 0.0f);
        SpecBlock tremolo    (float rateHz = 5.0f, float depth = 0.5f);
        SpecBlock delay      (float timeS = 0.3f, float feedback = 0.4f);
        SpecBlock reverb     (float size = 0.5f, float damping = 0.5f, float mix = 0.3f);
    }
}
