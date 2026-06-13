#include "SessionPlanner.h"

namespace
{
    // Mixer convention order — buses are created and laid out in this order.
    // "Instruments" = orchestral/winds family (flute, strings, brass, …) — they
    // are real instruments that fit no other family (user feedback 2026-06-12).
    const char* const kGroupOrder[] = { "Drums", "Bass", "Guitars", "Keys", "Synths",
                                        "Instruments", "Vocals", "FX" };

    bool isBusGroup (const juce::String& g)
    {
        for (auto* name : kGroupOrder)
            if (g == name)
                return true;
        return false;
    }
}

//==============================================================================
juce::Colour SessionPlanner::colourForGroup (const juce::String& group)
{
    // Stable, distinguishable hues per instrument family.
    if (group == "Drums")   return juce::Colour (0xffe06c4f);   // warm red-orange
    if (group == "Bass")    return juce::Colour (0xff9c6bd6);   // purple
    if (group == "Guitars") return juce::Colour (0xffd9a23c);   // amber
    if (group == "Keys")    return juce::Colour (0xff4fb0a5);   // teal
    if (group == "Synths")  return juce::Colour (0xffc95fc0);   // magenta
    if (group == "Instruments") return juce::Colour (0xffb08d57); // warm bronze
    if (group == "Vocals")  return juce::Colour (0xff4f8fd9);   // blue
    if (group == "FX")      return juce::Colour (0xff7fbf5a);   // green
    return juce::Colour (0xff8a8f98);                           // neutral grey (Other)
}

//==============================================================================
double SessionPlanner::groupOffsetDb (const juce::String& group, const ReferenceProfile& ref)
{
    if (! ref.isValid())
        return 0.0;

    // Map the profile's tonal emphasis onto the instrument families. Weights
    // are deliberately < 1 — the reference NUDGES the staging (REF-04:
    // guideline, not target). Clamped so an extreme profile can't wreck a mix.
    double offset = 0.0;
    if      (group == "Bass")        offset = ref.lowDb * 0.8;
    else if (group == "Drums")       offset = ref.lowDb * 0.4 + ref.highDb * 0.2;
    else if (group == "Guitars")     offset = ref.midDb * 0.6;
    else if (group == "Keys")        offset = ref.midDb * 0.5;
    else if (group == "Instruments") offset = ref.midDb * 0.5;
    else if (group == "Synths")      offset = ref.midDb * 0.3 + ref.highDb * 0.4;
    else if (group == "Vocals")      offset = ref.vocalDb;

    return juce::jlimit (-3.0, 3.0, offset);
}

//==============================================================================
SessionPlan SessionPlanner::plan (const juce::Array<StemInfo>& stems,
                                  const ReferenceProfile* reference)
{
    SessionPlan out;

    // --- Tracks: one per stem, duplicate names suffixed " 2", " 3", …
    juce::StringArray usedNames;

    for (const auto& s : stems)
    {
        PlannedTrack t;
        t.file         = s.file;
        t.group        = isBusGroup (s.group) ? s.group : juce::String();
        t.colour       = colourForGroup (s.group);
        t.confidence   = s.confidence;
        t.fromFilename = s.fromFilename;

        // Track name = the user's FILE name (user feedback 2026-06-12: replacing
        // real names like "135 bpm_Bassline" with generic labels forced manual
        // renaming). The detected instrument only drives group + colour.
        // Separated stems keep clean names anyway (their files ARE "Drums.wav" …).
        auto base = s.file.getFileNameWithoutExtension().isNotEmpty()
                        ? s.file.getFileNameWithoutExtension()
                        : s.instrument;
        auto name = base;
        for (int n = 2; usedNames.contains (name); ++n)
            name = base + " " + juce::String (n);
        usedNames.add (name);
        t.name = name;

        // --- Gain staging (AI-03): bring measured RMS to the target level,
        // nudged by the active reference/genre profile (REF-01/02).
        // Silence (or unreadable file, rmsDb = -120) is never boosted.
        double target = (s.group == "Vocals") ? kVocalRmsDb : kTargetRmsDb;
        if (reference != nullptr)
            target += groupOffsetDb (s.group, *reference);
        if (s.rmsDb > kSilenceRmsDb)
            t.gainDb = juce::jlimit (kMinGainDb, kMaxGainDb, target - s.rmsDb);

        out.tracks.add (t);
    }

    // --- Headroom: estimate the incoherent sum of all post-gain tracks.
    // N non-silent tracks at ~target RMS sum to roughly target + 10*log10(N);
    // if that exceeds the master budget, trim every non-silent gain equally
    // (relative balance preserved, silent tracks stay untouched).
    int active = 0;
    for (const auto& s : stems)
        if (s.rmsDb > kSilenceRmsDb)
            ++active;

    if (active > 1)
    {
        const double estimatedSum = kTargetRmsDb + 10.0 * std::log10 ((double) active);
        const double excess = estimatedSum - kMaxSumRmsDb;
        if (excess > 0.0)
            for (int i = 0; i < out.tracks.size(); ++i)
                if (stems[i].rmsDb > kSilenceRmsDb)
                    out.tracks.getReference (i).gainDb -= excess;
    }

    rebuildGroups (out);
    return out;
}

//==============================================================================
void SessionPlanner::rebuildGroups (SessionPlan& plan)
{
    plan.groups.clear();

    // Only the buses that actually have members, in mixer order.
    for (auto* g : kGroupOrder)
        for (const auto& t : plan.tracks)
            if (t.group == g)
            {
                plan.groups.add (g);
                break;
            }
}
