#pragma once

#include "MixingEngine.h"
#include "../engine/AICommandBridge.h"
#include "../engine/EditManager.h"

/**
 * MixApplyEngine — Phase 4 / Iteration 1, Slice 3.
 *
 * The bridge between the pure heuristic MixingEngine (Slice 2) and the live
 * Tracktion Engine. Two halves:
 *
 *   readProfiles()  — reads the live session into TrackMixProfiles (track id,
 *                     name, role/group, source RMS, current pan, lead vocal).
 *                     Does file I/O on clip sources → prefer a background context
 *                     for big sessions; TE-structure access stays on the message
 *                     thread (Slice 4 wires the threading).
 *
 *   applyPlan()     — turns a MixPlan into real TE mutations through the
 *                     AICommandBridge as ONE batch (= one undo step). Each
 *                     MixAction becomes a transaction-free core: SetTrackGain/Pan
 *                     drive the track's volume plugin; the EQ moves (HPF, carve,
 *                     boost, shelves) collapse into ONE Cloudes EQ per track;
 *                     Compress/DeEss insert the matching built-in FX; a Compress
 *                     targeting "Master" inserts mix-bus glue on the master list.
 *
 * AICommandBridge contract: applyMixNow() runs INSIDE the bridge's open
 * transaction and must never begin its own (mirrors StemImportEngine).
 */
class MixApplyEngine
{
public:
    MixApplyEngine (EditManager& em, AICommandBridge& bridge);

    /** A track to mix, gathered from the live Edit WITHOUT file I/O. */
    struct TrackScan
    {
        tracktion::engine::EditItemID id;
        juce::String name;
        juce::File   source;
        float        currentPan = 0.0f;
    };

    /** Message-thread, cheap: enumerate plain stem tracks (bus/FX/empty skipped),
        reading only TE structure — no audio decoding. */
    std::vector<TrackScan> scanTracks() const;

    /** Background-safe (no Edit access): measure source RMS, classify the group
        from the name, flag the lead vocal. Run this off the message thread. */
    static std::vector<cloudes::TrackMixProfile> profilesFromScan (const std::vector<TrackScan>& scan);

    /** Convenience (message thread): scanTracks() + profilesFromScan(). Does file
        I/O — prefer the split form for big sessions. Used by tests. */
    std::vector<cloudes::TrackMixProfile> readProfiles() const;

    /** Enqueue the whole plan as one AI batch (one undo step). Any thread. */
    void applyPlan (const cloudes::MixPlan& plan);

    /** Synchronous core — message thread, inside the bridge's open transaction. */
    void applyMixNow (const cloudes::MixPlan& plan, juce::UndoManager& um);

private:
    EditManager&     editManager;
    AICommandBridge& commandBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixApplyEngine)
};
