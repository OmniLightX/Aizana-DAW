#pragma once

#include "StemAnalyzer.h"
#include <juce_graphics/juce_graphics.h>

/**
 * SessionPlanner — Phase 3, Iteration 1, Slice 2 (AI-02 + AI-03).
 *
 * Pure planning layer: StemInfo in → SessionPlan out. NO Tracktion Engine
 * access, no I/O, no side effects — fully golden-testable. Slice 3 turns a
 * SessionPlan into one AICommandBridge batch (= one undo step, AI-04).
 *
 * Planning rules (pro defaults, user reviews in the Pro preview table — E3):
 *  - One track per stem, named after the detected instrument; duplicate names
 *    get " 2", " 3" suffixes.
 *  - Group buses (Named-Bus pattern) only for groups with members, in mixer
 *    convention order: Drums, Bass, Guitars, Keys, Vocals, FX.
 *    "Other" stems route directly to master (no artificial bus).
 *  - Gain staging: bring each track's measured RMS to the target level
 *    (−18 dBFS RMS, vocals −16). Clamped; silence is never boosted.
 *  - Headroom: if the estimated sum of all tracks exceeds the master budget,
 *    every gain is trimmed by the same amount (relative balance preserved).
 */
struct PlannedTrack
{
    juce::File   file;
    juce::String name;           // track name (from instrument label)
    juce::String group;          // bus name, or empty = direct to master
    double       gainDb = 0.0;   // initial fader gain (AI-03)
    juce::Colour colour;         // group colour (UI applies via TrackColours)
    double       confidence = 0.0;
    bool         fromFilename = false;
};

struct SessionPlan
{
    juce::Array<PlannedTrack> tracks;
    juce::StringArray         groups;   // buses to create, stable mixer order
};

class SessionPlanner
{
public:
    static SessionPlan plan (const juce::Array<StemInfo>& stems);

    // Tuning constants — exposed so tests assert against the same source of truth.
    static constexpr double kTargetRmsDb   = -18.0;  // per-track target after gain
    static constexpr double kVocalRmsDb    = -16.0;  // vocals sit slightly hotter
    static constexpr double kMaxSumRmsDb   = -12.0;  // estimated master sum budget
    static constexpr double kMaxGainDb     =  12.0;  // never boost more than this
    static constexpr double kMinGainDb     = -24.0;
    static constexpr double kSilenceRmsDb  = -60.0;  // below: leave gain at 0

    /** Stable colour per group (shared visual language with TrackColours palette idea). */
    static juce::Colour colourForGroup (const juce::String& group);
};
