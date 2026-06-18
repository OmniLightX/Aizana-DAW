#include "PluginSpec.h"

namespace cloudes
{
    juce::String SpecBlock::displayName() const
    {
        switch (kind)
        {
            case BlockKind::Gain:       return "Gain";
            case BlockKind::Filter:
                if (variant == "lowpass")   return "Lowpass";
                if (variant == "highpass")  return "Highpass";
                if (variant == "lowshelf")  return "Low Shelf";
                if (variant == "highshelf") return "High Shelf";
                if (variant == "peak")      return "Peak EQ";
                return "Filter";
            case BlockKind::Drive:      return "Saturation";
            case BlockKind::Compressor: return "Compressor";
            case BlockKind::Tremolo:    return "Tremolo";
            case BlockKind::Delay:      return "Delay";
            case BlockKind::Reverb:     return "Reverb";
        }
        return "Stage";
    }

    namespace Blocks
    {
        namespace
        {
            // freq | unit:Hz, log scale; gain | unit:dB, linear; plain value otherwise.
            SpecParam freq (const juce::String& key, const juce::String& label, float v)
            { return { key, label, "Hz", v, 20.0f, 20000.0f, 1.0f, true }; }

            SpecParam gainDbP (const juce::String& key, const juce::String& label, float v,
                               float lo = -24.0f, float hi = 24.0f)
            { return { key, label, "dB", v, lo, hi, 0.1f, false }; }
        }

        SpecBlock gain (float db)
        {
            return { BlockKind::Gain, "",
                     { gainDbP ("gain", "Gain", db) } };
        }

        SpecBlock lowpass (float cutoffHz)
        {
            return { BlockKind::Filter, "lowpass",
                     { freq ("cutoff", "Cutoff", cutoffHz) } };
        }

        SpecBlock highpass (float cutoffHz)
        {
            return { BlockKind::Filter, "highpass",
                     { freq ("cutoff", "Cutoff", cutoffHz) } };
        }

        SpecBlock lowShelf (float freqHz, float gainDb)
        {
            return { BlockKind::Filter, "lowshelf",
                     { freq ("freq", "Frequency", freqHz),
                       gainDbP ("gain", "Gain", gainDb) } };
        }

        SpecBlock highShelf (float freqHz, float gainDb)
        {
            return { BlockKind::Filter, "highshelf",
                     { freq ("freq", "Frequency", freqHz),
                       gainDbP ("gain", "Gain", gainDb) } };
        }

        SpecBlock peak (float freqHz, float gainDb, float q)
        {
            return { BlockKind::Filter, "peak",
                     { freq ("freq", "Frequency", freqHz),
                       gainDbP ("gain", "Gain", gainDb),
                       { "q", "Q", "", q, 0.1f, 10.0f, 0.01f, false } } };
        }

        SpecBlock driveCubic (float drive)
        {
            return { BlockKind::Drive, "cubic",
                     { { "drive", "Drive", "", drive, 0.0f, 0.99f, 0.01f, false } } };
        }

        SpecBlock driveTanh (float drive)
        {
            return { BlockKind::Drive, "tanh",
                     { { "drive", "Drive", "", drive, 1.0f, 20.0f, 0.01f, false } } };
        }

        SpecBlock compressor (float ratio, float thresholdDb, float attackMs,
                              float releaseMs, float makeupDb)
        {
            return { BlockKind::Compressor, "",
                     { { "ratio",     "Ratio",     "",   ratio,       1.0f,  20.0f,   0.1f,  false },
                       gainDbP ("threshold", "Threshold", thresholdDb, -60.0f, 0.0f),
                       { "attack",    "Attack",    "ms", attackMs,    0.1f,  100.0f,  0.1f,  false },
                       { "release",   "Release",   "ms", releaseMs,   1.0f,  1000.0f, 1.0f,  false },
                       gainDbP ("makeup", "Makeup", makeupDb, 0.0f, 24.0f) } };
        }

        SpecBlock tremolo (float rateHz, float depth)
        {
            return { BlockKind::Tremolo, "",
                     { { "rate",  "Rate",  "Hz", rateHz, 0.1f, 20.0f, 0.01f, false },
                       { "depth", "Depth", "",   depth,  0.0f, 1.0f,  0.01f, false } } };
        }

        SpecBlock delay (float timeS, float feedback)
        {
            return { BlockKind::Delay, "",
                     { { "time",     "Time",     "s", timeS,    0.01f, 2.0f,  0.001f, false },
                       { "feedback", "Feedback", "",  feedback, 0.0f,  0.95f, 0.01f,  false } } };
        }

        SpecBlock reverb (float size, float damping, float mix)
        {
            return { BlockKind::Reverb, "",
                     { { "size", "Size",    "", size,    0.0f, 1.0f, 0.01f, false },
                       { "damp", "Damping", "", damping, 0.0f, 1.0f, 0.01f, false },
                       { "mix",  "Mix",     "", mix,     0.0f, 1.0f, 0.01f, false } } };
        }
    }
}
