#include "LocalComposer.h"

namespace aizana
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
        bool       wantAir       = hasAny (p, { "bright", "brighten", "air", "treble", u8 ("höhen"),
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
        bool       wantCompress  = hasAny (p, { "compress", "compressor", "compression", "kompressor",
                                                "kompression", "glue", "punch", "tame", "level",
                                                "leveling", "dynamik", "dynamics", "squash", "tight" });
        const bool wantTremolo   = hasAny (p, { "tremolo", "wobble", "wobbel", "pulse", "pulsing",
                                                "shimmer" });
        const bool wantDelay     = hasAny (p, { "delay", "echo", u8 ("verzögerung"), "slap",
                                                "slapback", "ping pong", "pingpong", "repeat" });
        const bool wantReverb    = hasAny (p, { "reverb", "hall", "raum", "room", "space", "ambience",
                                                "ambient", "nachhall", "verhallen", "plate", "cavern" });

        // --- voice manipulation intents (DE/EN) ------------------------------------------
        bool wantDeEss     = hasAny (p, { "deess", "de-ess", "de ess", "sibilant", "zisch",
                                          "scharfe s", "scharfes s", "s-laut" });
        bool wantTelephone = hasAny (p, { "telefon", "telephone", "lofi", "lo-fi", "radio",
                                          "megafon", "megaphone", "megafon", "bullhorn" });
        bool wantRobot     = hasAny (p, { "robot", "roboter", "ringmod", "ring mod", "ring-mod",
                                          "vocoder", "daft", "metallic", "metallisch" });
        bool wantPitch     = hasAny (p, { "pitch", "autotune", "auto-tune", "transpose", "transponier",
                                          "oktave", "octave", u8 ("höher"), "tiefer", "deeper",
                                          "higher", "lower" });
        bool wantHarmony   = hasAny (p, { "harmon", "harmony", "harmonie", "terz", "quinte", "quint",
                                          "third", "fifth", "choir" });
        bool wantDoubler   = hasAny (p, { "double", "doubler", "doppel", "dopplung", "breiter",
                                          "thicken", "widen", "wide", "fett", "fatten", "stack" });

        // Pitch direction → concrete, audible default (no GUI yet to dial it in).
        float pitchSemis = 7.0f;
        if (hasAny (p, { "oktave hoch", "octave up" }))                          pitchSemis =  12.0f;
        else if (hasAny (p, { "oktave", "octave" }) && hasAny (p, { "tiefer", "runter", "down", "lower" }))
                                                                                 pitchSemis = -12.0f;
        else if (hasAny (p, { "tiefer", "deeper", "lower", "down" }))            pitchSemis =  -5.0f;
        else if (hasAny (p, { u8 ("höher"), "higher", "up" }))                   pitchSemis =   5.0f;

        // Generic "voice/vocal" request with nothing specific → a versatile, usable vocal
        // chain (de-ess → glue → air → subtle double) so the user's plugin actually does
        // something out of the box.
        const bool genericVocal = hasAny (p, { "stimme", "stimm", "vocal", "voice", "gesang",
                                               u8 ("sänger"), "saenger", "rap" });
        const bool anySpecific = wantHighpass || wantBass || wantAir || wantEq || wantDrive
                              || wantCompress || wantTremolo || wantDelay || wantReverb
                              || wantDeEss || wantTelephone || wantRobot || wantPitch
                              || wantHarmony || wantDoubler;
        if (genericVocal && ! anySpecific)
            wantDeEss = wantCompress = wantAir = wantDoubler = true;

        // --- assemble in canonical signal order ------------------------------------------
        if (wantHighpass)          spec.chain.push_back (Blocks::highpass());
        if (wantDeEss)             spec.chain.push_back (Blocks::deEsser());
        if (wantBass)              spec.chain.push_back (Blocks::lowShelf());
        if (wantEq && ! wantBass && ! wantAir)
                                   spec.chain.push_back (Blocks::peak());
        if (wantAir)               spec.chain.push_back (Blocks::highShelf());
        if (wantTelephone)         spec.chain.push_back (Blocks::telephone());
        if (wantPitch)             spec.chain.push_back (Blocks::pitchShift (pitchSemis));
        if (wantHarmony)           spec.chain.push_back (Blocks::harmonizer());
        if (wantRobot)             spec.chain.push_back (Blocks::ringMod());
        if (wantDrive)             spec.chain.push_back (wantTanh ? Blocks::driveTanh()
                                                                  : Blocks::driveCubic());
        if (wantCompress)          spec.chain.push_back (Blocks::compressor());
        if (wantDoubler)           spec.chain.push_back (Blocks::doubler());
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
