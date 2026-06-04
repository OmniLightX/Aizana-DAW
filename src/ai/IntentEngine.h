#pragma once

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"
#include <vector>

/**
 * IntentEngine — the translator (Phase 2, Block B).
 *
 * Turns a high-level musical *intent* ("bring the vocal forward") into a batch of
 * real, editable Tracktion-Engine mutations — exactly the editable mechanic a Pro
 * would build by hand. It produces a Plan (commands + human-readable description);
 * the caller hands the commands to the AICommandBridge, which applies them on the
 * message thread as ONE undo transaction. No black box: every intent = standard
 * plugins/params the user can then tweak.
 *
 * Phase 2 = HEURISTIC (rule-based). The concrete dB/ratio/freq values below are
 * sensible pro defaults and are intended to be refined by the mix engineer; the
 * interface is stable so Phase 3 can swap in ML behind it unchanged.
 *
 * Mechanism (verified against TE):
 *   - level/pan: AudioTrack::getVolumePlugin() volume/pan CachedValues (dB<->fader
 *     conversion), set with the bridge's UndoManager — NO beginNewTransaction
 *     (the bridge owns the per-batch transaction).
 *   - character/space/glue: *Cloudes'* own EQProcessor / ReverbProcessor /
 *     CompressorProcessor, inserted through TE's ExternalPlugin via BuiltInPluginFormat
 *     (the same path the mixer insert slots use) and configured on the live instance,
 *     then flushed to the ValueTree. Cloudes ships editors ONLY for its own processors,
 *     so the inserted mechanic is editable — the native te::*Plugin would be uneditable
 *     ("No editor available"), breaking the Reveal.
 */
class IntentEngine
{
public:
    enum class Category { LevelBalance, Character, SpaceDepth, ReferenceLoudness };

    enum class Intent
    {
        // Pegel & Balance
        VocalForward, QuieterInMix, CenterInMix,
        // Charakter / Klangfarbe
        Warmer, Clearer, MorePunch,
        // Raum & Tiefe
        MoreReverb, Wider, Closer,
        // Referenz & Loudness
        Louder, GlueMix, MatchPopReference,
    };

    struct Plan
    {
        bool                                  valid = false;
        juce::String                          description;  // undo label + Reveal title
        juce::String                          mechanic;     // "what I did" — Reveal body
        std::vector<AICommandBridge::Command> commands;
    };

    explicit IntentEngine (EditManager& editManager) : editManager (editManager) {}

    /**
     * Build a plan for an intent on a target track. For ReferenceLoudness intents the
     * target is ignored (they act on the master). If target is null, falls back to the
     * first audio track. An invalid Plan (valid == false) means nothing to do.
     */
    Plan plan (Intent intent, te::AudioTrack* target) const;

    // --- UI helpers (static, pure) ---
    static Category            category (Intent);
    static juce::String        label (Intent);            // short German UI label
    static juce::String        categoryLabel (Category);
    static std::vector<Intent> intentsFor (Category);

private:
    EditManager& editManager;

    te::AudioTrack* resolveTarget (te::AudioTrack* requested) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntentEngine)
};
