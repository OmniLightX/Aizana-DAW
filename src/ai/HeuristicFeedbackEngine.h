#pragma once

#include "ai/IntentEngine.h"
#include <vector>

class AudioEngine;

/**
 * HeuristicFeedbackEngine — the conversation (Phase 2, Block B).
 *
 * Turns the master meter (peak / RMS / LUFS, lock-free) into proactive, jargon-free
 * feedback for the Beginner Coach rail, each item optionally carrying a 1-click
 * IntentEngine::Intent so "the observation" and "the fix" are one tap apart.
 *
 * Design: the rules live in the PURE static evaluate(MeterSnapshot) so they are
 * testable without audio hardware. The live wrapper current() just samples the
 * master meter and forwards to evaluate(). The UI polls current() on its own timer
 * (engine = logic, UI = timing).
 *
 * Phase 2 = HEURISTIC: the dB/LUFS thresholds are sensible pro defaults intended to
 * be refined by the mix engineer; Phase 3 can replace evaluate() with ML behind the
 * same interface.
 */
class HeuristicFeedbackEngine
{
public:
    struct MeterSnapshot
    {
        float peakDb         = -100.0f;
        float rmsDb          = -100.0f;
        float lufsMomentary  = -100.0f;
        float lufsIntegrated = -100.0f;
    };

    struct Feedback
    {
        juce::String          message;                                  // jargon-free
        IntentEngine::Intent  suggestedIntent = IntentEngine::Intent::VocalForward;
        bool                  hasIntent = false;                        // guards suggestedIntent
    };

    explicit HeuristicFeedbackEngine (AudioEngine& audioEngine) : audioEngine (audioEngine) {}

    /** Pure heuristic rules — no audio required. Returns up to 3 ordered items. */
    static std::vector<Feedback> evaluate (const MeterSnapshot&);

    /** Sample the live master meter into a snapshot. */
    MeterSnapshot snapshot() const;

    /** Convenience: evaluate(snapshot()). Called by the UI on its timer. */
    std::vector<Feedback> current() const { return evaluate (snapshot()); }

private:
    AudioEngine& audioEngine;

    // Tunable thresholds (pro defaults — flagged for mix-engineer review).
    static constexpr float kSilenceLufs   = -50.0f;  // below this = effectively silent
    static constexpr float kQuietLufs     = -12.0f;  // quieter than competitive Pop (-9..-11)
    static constexpr float kNearClipDb    = -1.0f;   // peak above this = clipping risk
    static constexpr float kHighCrestDb   = 18.0f;   // peak-RMS gap above this = very dynamic
};
