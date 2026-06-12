#pragma once

#include "SessionPlanner.h"
#include "../engine/AICommandBridge.h"
#include "../engine/MixerEngine.h"

/**
 * StemImportEngine — Phase 3, Iteration 1, Slice 3 (AI-02/03/04).
 *
 * Turns a SessionPlan into a real session: tracks (named, coloured), imported
 * clips, group buses, routing and initial gains — applied through the
 * AICommandBridge as ONE batch, so the entire AI setup is ONE undo step (AI-04).
 *
 * THREADING: applyPlan() is safe from any thread (it only enqueues; the bridge
 * dispatches to the message thread). applyPlanNow() is the synchronous core and
 * runs INSIDE the bridge's already-open transaction — it must never begin its
 * own UndoManager transaction (AICommandBridge contract).
 *
 * Ordering invariant: stem tracks are created BEFORE the group buses so that
 * Bus channels stay at the END of the track list (track numbers must not shift).
 */
class StemImportEngine
{
public:
    StemImportEngine (EditManager& em, MixerEngine& mixer, AICommandBridge& bridge);

    /** Enqueue the whole plan as one AI batch (one undo step). */
    void applyPlan (const SessionPlan& plan);

    /** Synchronous core — exposed for tests and for the bridge command.
        Must be called on the message thread inside an open transaction. */
    void applyPlanNow (const SessionPlan& plan, juce::UndoManager& um);

private:
    EditManager&     editManager;
    MixerEngine&     mixerEngine;
    AICommandBridge& commandBridge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemImportEngine)
};
