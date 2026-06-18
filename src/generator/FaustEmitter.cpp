#include "FaustEmitter.h"

namespace cloudes
{
    namespace
    {
        // Deterministic number formatting: integers without a decimal point, otherwise up
        // to 4 trimmed decimals. Keeps golden output stable across platforms.
        juce::String num (float v)
        {
            if (v == std::floor (v) && std::abs (v) < 1.0e7f)
                return juce::String ((juce::int64) v);

            juce::String s (v, 4);
            while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
            if (s.endsWithChar ('.'))    s = s.dropLastCharacters (1);
            return s;
        }

        const SpecParam* find (const SpecBlock& b, const juce::String& key)
        {
            for (const auto& p : b.params)
                if (p.key == key)
                    return &p;
            return nullptr;
        }

        // hslider("Label[unit:Hz][scale:log]", value, min, max, step)
        juce::String slider (const SpecParam& p)
        {
            juce::String meta;
            if (p.unit.isNotEmpty()) meta << "[unit:" << p.unit << "]";
            if (p.logScale)          meta << "[scale:log]";

            return "hslider(\"" + p.label + meta + "\", "
                 + num (p.value) + ", " + num (p.min) + ", "
                 + num (p.max)   + ", " + num (p.step) + ")";
        }

        struct Stage
        {
            juce::String expr;                 // 2-in/2-out Faust expression
            juce::StringArray bindings;        // `name = …;` lines for `with {}`
            bool needsDryWet = false;          // requires the dw() helper
        };

        // Translate one block → a stereo stage. Local var names are scoped to the stage's
        // `with {}`, so short names are safe; vgroup() (added by emit) makes UI paths unique.
        Stage emitBlock (const SpecBlock& b)
        {
            Stage s;
            const auto bind = [&] (const char* name, const juce::String& rhs)
            { s.bindings.add (juce::String (name) + " = " + rhs + ";"); };

            switch (b.kind)
            {
                case BlockKind::Gain:
                {
                    bind ("g", "ba.db2linear(" + slider (*find (b, "gain")) + ")");
                    s.expr = "par(i, 2, *(g))";
                    break;
                }

                case BlockKind::Filter:
                {
                    if (b.variant == "lowpass" || b.variant == "highpass")
                    {
                        bind ("fc", slider (*find (b, "cutoff")));
                        s.expr = "par(i, 2, fi." + b.variant + "(2, fc))";
                    }
                    else if (b.variant == "lowshelf" || b.variant == "highshelf")
                    {
                        bind ("fc", slider (*find (b, "freq")));
                        bind ("g",  slider (*find (b, "gain")));
                        s.expr = "par(i, 2, fi." + b.variant + "(2, g, fc))";
                    }
                    else // peak
                    {
                        bind ("fc", slider (*find (b, "freq")));
                        bind ("g",  slider (*find (b, "gain")));
                        bind ("q",  slider (*find (b, "q")));
                        s.expr = "par(i, 2, fi.peak_eq_cq(g, fc, q))";
                    }
                    break;
                }

                case BlockKind::Drive:
                {
                    bind ("d", slider (*find (b, "drive")));
                    s.expr = b.variant == "tanh"
                               ? "par(i, 2, \\(x).(ma.tanh(x * d) / ma.tanh(d)))"
                               : "par(i, 2, ef.cubicnl(d, 0))";
                    break;
                }

                case BlockKind::Compressor:
                {
                    bind ("ratio", slider (*find (b, "ratio")));
                    bind ("thr",   slider (*find (b, "threshold")));
                    bind ("att",   slider (*find (b, "attack"))  + " / 1000");
                    bind ("rel",   slider (*find (b, "release")) + " / 1000");
                    bind ("mk",    "ba.db2linear(" + slider (*find (b, "makeup")) + ")");
                    s.expr = "co.compressor_stereo(ratio, thr, att, rel) : par(i, 2, *(mk))";
                    break;
                }

                case BlockKind::Tremolo:
                {
                    bind ("rate",  slider (*find (b, "rate")));
                    bind ("depth", slider (*find (b, "depth")));
                    bind ("lfo",   "(1 - depth) + depth * 0.5 * (1 + os.osc(rate))");
                    s.expr = "par(i, 2, *(lfo))";
                    break;
                }

                case BlockKind::Delay:
                {
                    bind ("t",  slider (*find (b, "time")));
                    bind ("fb", slider (*find (b, "feedback")));
                    s.expr = "par(i, 2, ef.echo(2.0, t, fb))";
                    break;
                }

                case BlockKind::Reverb:
                {
                    bind ("sz",  slider (*find (b, "size")));
                    bind ("dmp", slider (*find (b, "damp")));
                    bind ("mix", slider (*find (b, "mix")));
                    s.expr = "dw(mix, re.stereo_freeverb(sz, sz, dmp, 0.5))";
                    s.needsDryWet = true;
                    break;
                }
            }

            return s;
        }
    }

    juce::String FaustEmitter::emit (const PluginSpec& spec)
    {
        const auto name = spec.name.replace ("\"", "").replace ("\n", " ");

        juce::String out;
        out << "declare name \"" << name << "\";\n"
            << "declare author \"Cloudes\";\n"
            << "import(\"stdfaust.lib\");\n\n";

        if (spec.chain.empty())
        {
            out << "process = _, _;\n";
            return out;
        }

        std::vector<Stage> stages;
        stages.reserve (spec.chain.size());
        bool anyDryWet = false;
        for (const auto& b : spec.chain)
        {
            stages.push_back (emitBlock (b));
            anyDryWet = anyDryWet || stages.back().needsDryWet;
        }

        // Stereo dry/wet helper: dw(mix, fx) blends 2-in/2-out `fx` against the dry signal.
        if (anyDryWet)
            out << "dw(mix, fx) = _, _ <: (*(1 - mix), *(1 - mix)), "
                   "(fx : *(mix), *(mix)) :> _, _;\n\n";

        for (size_t i = 0; i < stages.size(); ++i)
        {
            out << "stage" << (int) i << " = " << stages[i].expr << " with {\n";
            for (const auto& line : stages[i].bindings)
                out << "    " << line << "\n";
            out << "};\n\n";
        }

        out << "process = ";
        for (size_t i = 0; i < stages.size(); ++i)
        {
            if (i > 0) out << " : ";
            out << "vgroup(\"" << (int) i << " " << spec.chain[i].displayName()
                << "\", stage" << (int) i << ")";
        }
        out << ";\n";

        return out;
    }
}
