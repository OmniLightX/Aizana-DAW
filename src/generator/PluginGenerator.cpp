#include "PluginGenerator.h"

namespace cloudes
{
    const std::vector<GeneratorTemplate>& PluginGenerator::templates()
    {
        // Curated B1 set — one entry per generator/templates/*.dsp. Keywords are DE/EN
        // and lower-case; the matcher lower-cases the prompt before comparing.
        // NOTE: non-ASCII literals MUST go through fromUTF8 — juce::String(const char*)
        // does not decode UTF-8 in this build (umlauts render as "Ã¤"). Applies to both
        // the displayed descriptions AND keywords used by the matcher.
        const auto u8 = [] (const char* s) { return juce::String::fromUTF8 (s); };

        // Real, full plugins (Faust-compiled) — built-in calibre, not toys. Each .dsp in
        // generator/templates/ has a matching entry here. id == .dsp/.clap basename.
        static const std::vector<GeneratorTemplate> kTemplates {
            { "compressor", "Compressor", "Dynamics",
              u8 ("Dynamik bändigen — Threshold, Ratio, Attack, Release, Makeup"),
              { "compressor", "compression", "compress", "kompressor", "kompression",
                "dynamics", "dynamik", "glue", "punch", "leveling", "tame", "level" } },

            { "eq3", "EQ (3-Band)", "EQ",
              u8 ("3-Band-EQ — Bässe, Mitten, Höhen formen"),
              { "eq", "equalizer", "equalize", "frequency", "frequenz", "bass", u8 ("bässe"),
                "mid", "mitten", u8 ("höhen"), "treble", "tone", "klang", "shelf", "bell",
                "brighten", "dunkler", "filter", "tiefpass", "highcut" } },

            { "reverb", "Reverb", "Reverb",
              u8 ("Raum & Hall — Size, Damping, Mix"),
              { "reverb", "hall", "raum", "room", "space", "ambience", "ambient",
                "verhallen", "wet", "nachhall" } },

            { "delay", "Delay", "Delay",
              u8 ("Echo mit Feedback — Time, Feedback, Mix"),
              { "delay", "echo", u8 ("verzögerung"), "feedback", "repeat", "slap", "ping",
                "pong" } },

            { "drive", "Saturation / Drive", "Saturation",
              u8 ("Sättigung/Verzerrung für Wärme und Druck"),
              { "drive", "distortion", "distort", "saturation", "saturate", "overdrive",
                "verzerrung", "verzerren", u8 ("sättigung"), "saturieren", "dirt", "crunch",
                "fuzz", "tube", u8 ("röhre"), "druck", "warm", "warmth" } },
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
