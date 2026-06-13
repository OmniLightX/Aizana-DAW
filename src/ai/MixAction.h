#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace cloudes
{
    /**
        MixAction — one named, typed, reviewable mixing move.

        Phase 4 / Iteration 1, Slice 1. This is the MCP-ready command vocabulary
        the KI-Mixing co-pilot speaks: every move is a value with a stable name(),
        typed parameters, a target track, and a jargon-free `reason`. It is
        deliberately decoupled from how it is applied — the heuristic engine
        (Slice 2) produces MixActions, the AICommandBridge translation (Slice 3)
        consumes them. The same vocabulary is later exposable as MCP tools without
        a rewrite (local heuristic / local ML / opt-in LLM all emit MixActions).

        Pure data — no engine, no Tracktion mutation here.
    */
    enum class MixActionType
    {
        SetTrackGain,    // gainDb  = target track gain in dB
        SetTrackPan,     // pan     = -1 (L) .. +1 (R)
        HighPassFilter,  // freqHz  = high-pass corner (low cut)
        CarveBand,       // freqHz, gainDb (< 0), q  — cut mud/resonance (bell)
        BoostBand,       // freqHz, gainDb (> 0), q  — presence (bell)
        LowShelf,        // freqHz, gainDb (±), q    — broad low-end shaping
        HighShelf,       // freqHz, gainDb (±), q    — air / top-end (preferred over a bell up high)
        Compress,        // thresholdDb, ratio, makeupGainDb (target "Master" = mix-bus glue)
        DeEss,           // freqHz (~5.5 kHz), thresholdDb, ratio — tame sibilance (Grammy-universal on vocals)
    };

    struct MixAction
    {
        MixActionType type {};
        tracktion::engine::EditItemID targetTrack;   // stable track id
        juce::String targetName;                      // human/tool-readable label

        // Typed params — only those relevant to `type` carry meaning.
        float gainDb      = 0.0f;
        float pan         = 0.0f;
        float freqHz      = 0.0f;
        float q           = 0.7f;
        float thresholdDb = 0.0f;
        float ratio       = 1.0f;
        float makeupGainDb = 0.0f;   // Compress: gain back the level the ratio took

        juce::String reason;   // jargon-free rationale (beginner learns / pro reviews)

        /** Stable, MCP-ready action name (snake_case, never localised). */
        juce::String name() const
        {
            switch (type)
            {
                case MixActionType::SetTrackGain:   return "set_track_gain";
                case MixActionType::SetTrackPan:    return "set_track_pan";
                case MixActionType::HighPassFilter: return "high_pass_filter";
                case MixActionType::CarveBand:      return "carve_band";
                case MixActionType::BoostBand:      return "boost_band";
                case MixActionType::LowShelf:       return "low_shelf";
                case MixActionType::HighShelf:      return "high_shelf";
                case MixActionType::Compress:       return "compress";
                case MixActionType::DeEss:          return "de_ess";
            }
            return "unknown";
        }

        /** Human-readable one-liner of the move + its key parameter (for the
            Pro review list). The friendly *why* lives in `reason`. */
        juce::String describe() const
        {
            const auto t = targetName.isNotEmpty() ? targetName : juce::String ("Track");

            switch (type)
            {
                case MixActionType::SetTrackGain:
                    return t + ": Pegel auf " + juce::String (gainDb, 1) + " dB";
                case MixActionType::SetTrackPan:
                    return t + ": Pan " + panLabel();
                case MixActionType::HighPassFilter:
                    return t + ": Hochpass bei " + juce::String (juce::roundToInt (freqHz)) + " Hz";
                case MixActionType::CarveBand:
                    return t + ": " + juce::String (gainDb, 1) + " dB bei "
                             + juce::String (juce::roundToInt (freqHz)) + " Hz";
                case MixActionType::BoostBand:
                    return t + ": +" + juce::String (gainDb, 1) + " dB bei "
                             + juce::String (juce::roundToInt (freqHz)) + " Hz";
                case MixActionType::LowShelf:
                    return t + ": Low-Shelf " + signedDb (gainDb) + " ab "
                             + juce::String (juce::roundToInt (freqHz)) + " Hz";
                case MixActionType::HighShelf:
                    return t + ": High-Shelf " + signedDb (gainDb) + " ab "
                             + juce::String (juce::roundToInt (freqHz)) + " Hz";
                case MixActionType::Compress:
                {
                    auto s = t + ": Kompressor " + juce::String (ratio, 1) + ":1 @ "
                               + juce::String (thresholdDb, 1) + " dB";
                    if (makeupGainDb > 0.05f)
                        s += ", +" + juce::String (makeupGainDb, 1) + " dB Makeup";
                    return s;
                }
                case MixActionType::DeEss:
                    return t + ": De-Esser bei "
                             + juce::String (juce::roundToInt (freqHz)) + " Hz";
            }
            return t;
        }

    private:
        /** Signed dB with an explicit '+' for boosts (shelves cut or boost). */
        static juce::String signedDb (float db)
        {
            return (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        }

        juce::String panLabel() const
        {
            if (std::abs (pan) < 0.02f) return "Mitte";
            const int pct = juce::roundToInt (std::abs (pan) * 100.0f);
            return juce::String (pct) + "% " + (pan < 0.0f ? "L" : "R");
        }
    };

    /**
        MixPlan — an ordered set of MixActions plus a short human summary. The unit
        the co-pilot proposes; applied as ONE AICommandBridge batch = one undo step.
    */
    struct MixPlan
    {
        std::vector<MixAction> actions;
        juce::String summary;

        bool isEmpty() const noexcept { return actions.empty(); }
        int  size()    const noexcept { return (int) actions.size(); }
        void add (MixAction a)        { actions.push_back (std::move (a)); }
    };
}
