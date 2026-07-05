#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace aizana
{
    /**
        PluginGenerator — Phase 4 / Iteration B, B1 (backend).

        The in-app entry to the Text→Faust→CLAP generator. B1 ships a curated set of
        Faust templates compiled to self-contained .clap at build time (bundled in
        Contents/Resources/Generator). This class is the pure, testable backend:
        a template registry + a DE/EN keyword matcher that turns a free-text request
        ("mach es lauter", "add distortion") into a template choice. B2 loads the
        resolved .clap onto a track via ClapPluginFormat; B3 swaps the precompiled
        templates for real runtime Faust generation (libfaust) — the registry, matcher
        and loading path are reused unchanged.

        MCP-ready: the matcher mirrors IntentEngine::parsePrompt — an LLM/MCP proposer
        can later select a template (and parameters) through the same vocabulary.
    */
    struct GeneratorTemplate
    {
        juce::String id;            // "gain" — matches the .dsp / .clap basename
        juce::String displayName;   // "Gain"
        juce::String category;      // "Dynamics" | "Filter" | "Saturation" | …
        juce::String description;   // jargon-free one-liner
        juce::StringArray keywords; // DE/EN trigger words for the text matcher

        juce::String clapFileName() const { return id + ".clap"; }
    };

    class PluginGenerator
    {
    public:
        /** The curated template set (B1). Kept in sync with generator/templates/*.dsp. */
        static const std::vector<GeneratorTemplate>& templates();

        /** Best template for a free-text prompt (DE/EN keyword match), or nullptr if
            nothing matches. Pure — no I/O. */
        static const GeneratorTemplate* match (const juce::String& prompt);

        /** Look up a template by id, or nullptr. */
        static const GeneratorTemplate* byId (const juce::String& id);

        /** Directory holding the bundled .clap inside the running .app
            (Contents/Resources/Generator). Runtime helper (not used in headless tests). */
        static juce::File bundledGeneratorDir();

        /** Resolved bundled .clap file for a template (may not exist if generator was
            built with AIZANA_BUILD_GENERATOR=OFF). */
        static juce::File clapFileFor (const GeneratorTemplate&);
    };
}
