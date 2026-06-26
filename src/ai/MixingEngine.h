#pragma once

#include "MixAction.h"
#include "ReferenceProfile.h"
#include "StemAnalyzer.h"     // StemSide
#include "IInferenceBackend.h"
#include <vector>

namespace cloudes
{
    /**
        MixingEngine — Phase 4 / Iteration 1, Slice 2.

        Pure heuristic mixing brain: a snapshot of the session's tracks in,
        a MixPlan (ordered MixActions + summary) out. NO Tracktion Engine access,
        no I/O, no side effects — fully golden-testable, exactly like SessionPlanner.
        Slice 3 turns the MixPlan into ONE AICommandBridge batch (= one undo step);
        Slice 4 wires the "Mix" button (Beginner = apply now, Pro = review).

        The defaults are oriented to documented practice of Grammy-tier US engineers
        (Ghenea, Marroquin, Chris Lord-Alge, Andrew Scheps) — subtractive-EQ-first,
        forward de-essed/serially-compressed vocals, role-based panning, and a gentle
        master-bus glue (the "mix into the bus" philosophy). Every move carries a
        jargon-free `reason`; an optional ReferenceProfile nudges the targets
        (genre or analysed reference, ± clamped, never enforced — REF-04).
    */
    struct TrackMixProfile
    {
        tracktion::engine::EditItemID id;     // stable track id (bridge target)
        juce::String name;                    // track / human label
        juce::String group;                   // role bucket (StemAnalyzer groups)
        juce::String instrument;              // specific element ("Kick", "HiHat", "Snare", …)
        juce::String subRole;                 // pro sub-role within the group ("Backing", "Double", …)
        StemSide side = StemSide::None;        // L/R from the name (hard pan / stereo pair)
        double rmsDb      = -120.0;           // measured, for gain staging
        double peakDb     = -120.0;
        float  currentPan = 0.0f;             // -1 (L) .. +1 (R); respected if already set
        bool   isLeadVocal = false;           // the one vocal that sits up front + centre
    };

    struct MixOptions
    {
        // User decision 2026-06-13: the Mix co-pilot MAY put gentle glue
        // compression on the master (Beginner + Pro, reviewable). The
        // SessionPlanner never touches the master — this is a different role.
        bool allowMasterGlue = true;
        tracktion::engine::EditItemID masterId;   // glue target (resolved by Slice 3)
        juce::String masterName = "Master";
    };

    class MixingEngine
    {
    public:
        using Options = MixOptions;

        /** Builds the mix plan. `reference` (genre preset or analysed track) nudges
            the per-role targets; nullptr = neutral pro defaults. `backend`, when
            non-null and available, refines gain-trim magnitudes (Slice 5a); nullptr
            or unavailable = pure heuristic (byte-identical to the golden tests).
            Call off the audio thread — backend inference runs synchronously here. */
        static MixPlan mix (const std::vector<TrackMixProfile>& tracks,
                            const ReferenceProfile* reference = nullptr,
                            const MixOptions& options = {},
                            IInferenceBackend* backend = nullptr);

        //======================================================================
        enum class Role { Drums, Bass, Guitars, Keys, Synths, Instruments, Vocals, FX, Other };

        /** Maps a StemAnalyzer group string to a mixing role. Exposed for tests. */
        static Role roleForGroup (const juce::String& group);

        //======================================================================
        // Tuning constants — single source of truth; tests assert against these.

        // Gain staging (matches SessionPlanner's language; re-balances the live mix).
        static constexpr double kVocalRmsDb   = -16.0;   // vocals sit a touch hotter
        static constexpr double kDefaultRmsDb = -18.0;   // everything else
        static constexpr double kMaxGainDb    =  12.0;
        static constexpr double kMinGainDb    = -24.0;
        static constexpr double kSilenceRmsDb = -60.0;   // below: leave the track alone
        static constexpr double kMinGainMoveDb = 0.5;    // skip no-op fader moves
        static constexpr float  kMlTrimClampDb = 2.0f;   // max ± the ML backend may nudge a gain move (Slice 5a)

        // EQ
        static constexpr float kMudFreqHz       = 300.0f;
        static constexpr float kMudCutDb        = -2.5f;  // subtractive-first
        static constexpr float kMudQ            = 1.0f;
        static constexpr float kVocalPresenceHz = 4000.0f;
        static constexpr float kVocalPresenceDb = 2.0f;
        static constexpr float kVocalPresenceQ  = 0.7f;   // wide bell (user feedback)
        static constexpr float kVocalAirHz      = 12000.0f;
        static constexpr float kVocalAirDb      = 1.5f;   // high-shelf, not a bell
        static constexpr float kDeEssHz         = 5500.0f;
        static constexpr float kDeEssThreshDb   = -20.0f;
        static constexpr float kDeEssRatio      = 4.0f;

        // Compression
        static constexpr float kVocalCompThreshDb = -19.0f;  // 1176-style peak stage
        // Master glue (CLA/Scheps "mix into the bus")
        static constexpr float kGlueRatio   = 2.0f;
        static constexpr float kGlueThreshDb = -12.0f;
        static constexpr float kGlueMakeupDb = 1.0f;

        // REF-03 — style matching: gentle master tonal tilt toward the reference's
        // low/high balance (the per-track moves already carry most of it, so this is a
        // light finishing touch). REF-04: a guideline, clamped, never enforced.
        static constexpr double kRefMasterTiltScale = 0.5;     // reference dB → master shelf dB
        static constexpr double kRefMasterTiltMaxDb = 2.0;     // hard clamp on the tilt
        static constexpr double kRefMasterTiltMinDb = 0.5;     // below: skip (no audible move)
        static constexpr float  kRefMasterLowHz     = 100.0f;
        static constexpr float  kRefMasterHighHz    = 10000.0f;
    };
}
