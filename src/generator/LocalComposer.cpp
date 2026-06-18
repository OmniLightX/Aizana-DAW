#include "LocalComposer.h"

namespace cloudes
{
    namespace
    {
        // UTF-8 literals MUST go through fromUTF8 — juce::String(const char*) does not
        // decode UTF-8 in this build (umlauts mojibake). Same convention as PluginGenerator.
        juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

        bool hasAny (const juce::String& p, const juce::StringArray& words)
        {
            for (const auto& w : words)
                if (p.contains (w.toLowerCase()))
                    return true;
            return false;
        }
    }

    PluginSpec LocalComposer::compose (const juce::String& prompt)
    {
        const auto p = prompt.toLowerCase();
        PluginSpec spec;

        // --- detect intents (DE/EN) -----------------------------------------------------
        const bool wantHighpass  = hasAny (p, { "highpass", "high pass", "high-pass", "low cut",
                                                 "lowcut", "rumble", "mud", "mulm", "hochpass" });
        const bool wantBass      = hasAny (p, { "bass", u8 ("bässe"), "low end", "lowend",
                                                "sub", "fundament", "boom" });
        const bool wantAir       = hasAny (p, { "bright", "brighten", "air", "treble", u8 ("höhen"),
                                                "presence", "shine", "crisp", "brillanz", "sparkle" });
        const bool wantEq        = hasAny (p, { "eq", "equalizer", "equalize", "mid", "mitten",
                                                "frequency", "frequenz", "nasal" });
        const bool wantDrive     = hasAny (p, { "distortion", "distort", "saturation", "saturate",
                                                "overdrive", "drive", "verzerrung", "verzerren",
                                                u8 ("sättigung"), "dirt", "crunch", "fuzz", "tube",
                                                u8 ("röhre"), "warm", "warmth", u8 ("wärme"),
                                                "tape", "grit", "color", "colour" });
        const bool wantTanh      = hasAny (p, { "warm", "warmth", u8 ("wärme"), "tube", u8 ("röhre"),
                                                "tape", "smooth", "soft", "sanft" });
        const bool wantCompress  = hasAny (p, { "compress", "compressor", "compression", "kompressor",
                                                "kompression", "glue", "punch", "tame", "level",
                                                "leveling", "dynamik", "dynamics", "squash", "tight" });
        const bool wantTremolo   = hasAny (p, { "tremolo", "wobble", "wobbel", "pulse", "pulsing",
                                                "shimmer" });
        const bool wantDelay     = hasAny (p, { "delay", "echo", u8 ("verzögerung"), "slap",
                                                "slapback", "ping pong", "pingpong", "repeat" });
        const bool wantReverb    = hasAny (p, { "reverb", "hall", "raum", "room", "space", "ambience",
                                                "ambient", "nachhall", "verhallen", "plate", "cavern" });

        // --- assemble in canonical signal order ------------------------------------------
        if (wantHighpass)          spec.chain.push_back (Blocks::highpass());
        if (wantBass)              spec.chain.push_back (Blocks::lowShelf());
        if (wantEq && ! wantBass && ! wantAir)
                                   spec.chain.push_back (Blocks::peak());
        if (wantAir)               spec.chain.push_back (Blocks::highShelf());
        if (wantDrive)             spec.chain.push_back (wantTanh ? Blocks::driveTanh()
                                                                  : Blocks::driveCubic());
        if (wantCompress)          spec.chain.push_back (Blocks::compressor());
        if (wantTremolo)           spec.chain.push_back (Blocks::tremolo());
        if (wantDelay)             spec.chain.push_back (Blocks::delay());
        if (wantReverb)            spec.chain.push_back (Blocks::reverb());

        // Unrecognised → neutral, always-loadable utility.
        if (spec.chain.empty())
            spec.chain.push_back (Blocks::gain());

        // Name from the assembled stages: "Saturation + Delay + Reverb".
        juce::StringArray parts;
        for (const auto& b : spec.chain)
            parts.add (b.displayName());
        spec.name = parts.joinIntoString (" + ");

        return spec;
    }
}
