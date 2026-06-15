#include "PluginGenerator.h"

namespace cloudes
{
    const std::vector<GeneratorTemplate>& PluginGenerator::templates()
    {
        // Curated B1 set — one entry per generator/templates/*.dsp. Keywords are DE/EN
        // and lower-case; the matcher lower-cases the prompt before comparing.
        static const std::vector<GeneratorTemplate> kTemplates {
            { "gain", "Gain", "Dynamics",
              "Lautstärke anheben oder absenken",
              { "gain", "volume", "level", "loud", "louder", "quieter",
                "lauter", "leiser", "pegel", "lautstärke", "boost" } },

            { "lowpass", "Low-Pass Filter", "Filter",
              "Höhen wegnehmen, Klang abdunkeln",
              { "lowpass", "low-pass", "low pass", "filter", "tiefpass", "highcut",
                "high cut", "dark", "darker", "dumpf", "dunkel", "muffle", "warm",
                "höhen", "weicher" } },

            { "drive", "Saturation / Drive", "Saturation",
              "Sättigung/Verzerrung für Wärme und Druck",
              { "drive", "distortion", "distort", "saturation", "saturate", "overdrive",
                "verzerrung", "verzerren", "sättigung", "saturieren", "dirt", "crunch",
                "fuzz", "tube", "röhre", "druck" } },
        };
        return kTemplates;
    }

    const GeneratorTemplate* PluginGenerator::byId (const juce::String& id)
    {
        for (const auto& t : templates())
            if (t.id == id)
                return &t;
        return nullptr;
    }

    const GeneratorTemplate* PluginGenerator::match (const juce::String& prompt)
    {
        const auto p = prompt.toLowerCase();
        if (p.isEmpty())
            return nullptr;

        const GeneratorTemplate* best = nullptr;
        int bestScore = 0;

        for (const auto& t : templates())
        {
            int score = 0;
            for (const auto& kw : t.keywords)
                if (p.contains (kw.toLowerCase()))
                    ++score;

            if (score > bestScore)   // first template wins ties (registry order = priority)
            {
                bestScore = score;
                best = &t;
            }
        }
        return best;   // nullptr if nothing matched
    }

    juce::File PluginGenerator::bundledGeneratorDir()
    {
        // On macOS currentApplicationFile is the .app bundle; templates live in
        // Contents/Resources/Generator (see CMake POST_BUILD copy).
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                   .getChildFile ("Contents/Resources/Generator");
    }

    juce::File PluginGenerator::clapFileFor (const GeneratorTemplate& t)
    {
        return bundledGeneratorDir().getChildFile (t.clapFileName());
    }
}
