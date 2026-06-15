#pragma once

#include "MixingEngine.h"   // TrackMixProfile, MixPlan, MixAction
#include <juce_core/juce_core.h>
#include <vector>

namespace cloudes
{
    /**
        MixDataLogger — Phase 4: the training-data flywheel (Slice „Daten aus Ergebnissen").

        Every reviewed mix produces labelled examples: for each proposed MixAction we log
        the track's feature snapshot (the model-input contract from MixFeatures.h), the
        action + its params, and the USER VERDICT (accepted / rejected). In Pro mode the
        verdict comes from the MixPreviewDialog toggles (the strong signal = the user's own
        taste); in Beginner mode every move is accepted by default (weaker, marked as such).

        This is what makes Slice 5b (a real local model) possible without a pre-existing
        dataset: the heuristic — and later an MCP/LLM proposer — proposes, the user judges,
        and the accepted/rejected pairs accumulate as a local JSONL corpus that a model is
        later distilled from. Local-first: written only to the user's machine, never uploaded.

        Pure core (toJsonLine) is golden-testable; file append is a thin wrapper. JSON Lines
        (one example per line) so the corpus is append-only and trivial to consume for training.
    */
    struct MixLogContext
    {
        juce::String proposer = "heuristic";   // who proposed the move ("heuristic" / later "llm" / "ml")
        juce::String mode;                      // "pro" (reviewed) | "beginner" (auto-accepted)
        juce::String genre;                     // active reference/genre, if any
        juce::String timestamp;                 // ISO8601; filled per batch if empty
    };

    class MixDataLogger
    {
    public:
        /** Default corpus location: ~/Library/Application Support/Cloudes/training/mix_decisions.jsonl */
        static juce::File defaultLogFile();

        /** One JSONL line for a single (track, action, verdict). Pure — no I/O. */
        static juce::String toJsonLine (const TrackMixProfile& track,
                                        const MixAction& action,
                                        bool accepted,
                                        const MixLogContext& ctx);

        /** Logs a reviewed plan: one line per PROPOSED action, with accepted = whether that
            action survived into `accepted` (the applied/edited plan). Beginner mode passes
            accepted == proposed. Appends to `logFile`; returns the number of lines written.
            Never throws; logging failures must not disrupt the mix. */
        static int logReview (const std::vector<TrackMixProfile>& profiles,
                              const MixPlan& proposed,
                              const MixPlan& accepted,
                              MixLogContext ctx,
                              const juce::File& logFile = defaultLogFile());

    private:
        static const TrackMixProfile* findProfile (const std::vector<TrackMixProfile>&,
                                                   const MixAction&);
        static juce::String signature (const MixAction&);   // for the accept/reject match
    };
}
