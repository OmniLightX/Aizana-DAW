#pragma once

#include <juce_data_structures/juce_data_structures.h>

/**
 * ReferenceProfile — Phase 3, Iteration 2 (REF-01/REF-02/REF-04).
 *
 * The sonic anchor for AI decisions: either a built-in genre profile (REF-02)
 * or measured from a user-imported reference track (REF-01). Persisted in the
 * Edit's ValueTree so it survives save/load.
 *
 * REF-04 contract: targetLufs and the band balance are REFERENCE INFORMATION —
 * guidelines the AI reads (e.g. gain-staging emphasis, coach feedback), NEVER
 * enforced targets. Nothing in Aizana auto-normalises the master to them.
 */
struct ReferenceProfile
{
    juce::String name;                 // "Hip-Hop", "Rock", … or the reference file name
    bool         fromReferenceTrack = false;

    double targetLufs = -14.0;         // info only (REF-04)

    // Relative tonal emphasis around 0 dB (low ≈ 20-250 Hz, mid ≈ 250 Hz-4 kHz,
    // high ≈ 4-20 kHz) plus a vocal-forwardness hint. Small numbers by design —
    // these nudge the gain staging, they don't mix the song.
    double lowDb   = 0.0;
    double midDb   = 0.0;
    double highDb  = 0.0;
    double vocalDb = 0.0;

    bool isValid() const noexcept { return name.isNotEmpty(); }

    //==========================================================================
    static constexpr const char* kStateType = "AIZANAREF";

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree v (kStateType);
        v.setProperty ("name",       name,               nullptr);
        v.setProperty ("fromTrack",  fromReferenceTrack, nullptr);
        v.setProperty ("targetLufs", targetLufs,         nullptr);
        v.setProperty ("lowDb",      lowDb,              nullptr);
        v.setProperty ("midDb",      midDb,              nullptr);
        v.setProperty ("highDb",     highDb,             nullptr);
        v.setProperty ("vocalDb",    vocalDb,            nullptr);
        return v;
    }

    static ReferenceProfile fromValueTree (const juce::ValueTree& v)
    {
        ReferenceProfile p;
        if (! v.hasType (juce::Identifier (kStateType)))
            return p;
        p.name               = v.getProperty ("name").toString();
        p.fromReferenceTrack = (bool)   v.getProperty ("fromTrack", false);
        p.targetLufs         = (double) v.getProperty ("targetLufs", -14.0);
        p.lowDb              = (double) v.getProperty ("lowDb", 0.0);
        p.midDb              = (double) v.getProperty ("midDb", 0.0);
        p.highDb             = (double) v.getProperty ("highDb", 0.0);
        p.vocalDb            = (double) v.getProperty ("vocalDb", 0.0);
        return p;
    }

    //==========================================================================
    // REF-02: built-in genre profiles (pro defaults — user reviews results).
    // Tuned to CURRENT mix conventions (user feedback 2026-06-13: modern rap
    // mixes carry very present vocals; one "Hip-Hop" bucket is too coarse).
    // For anything not listed: analyse a reference track instead (REF-01).
    static ReferenceProfile genre (const juce::String& genreName)
    {
        ReferenceProfile p;
        p.name = genreName;

        if (genreName == "Hip-Hop / Rap")
        {
            // modern rap: vocals sit on top of the beat, 808 carries the low end
            p.targetLufs = -9.0;  p.lowDb = 2.0;  p.midDb = -0.5; p.highDb = 0.5; p.vocalDb = 2.5;
        }
        else if (genreName == "Trap / Phonk")
        {
            p.targetLufs = -8.0;  p.lowDb = 3.0;  p.midDb = -1.0; p.highDb = 1.0; p.vocalDb = 2.0;
        }
        else if (genreName == "R&B")
        {
            p.targetLufs = -10.0; p.lowDb = 1.5;  p.midDb = 0.5;  p.highDb = 0.5; p.vocalDb = 2.5;
        }
        else if (genreName == "Pop")
        {
            p.targetLufs = -9.0;  p.lowDb = 0.5;  p.midDb = 0.5;  p.highDb = 1.0; p.vocalDb = 2.5;
        }
        else if (genreName == "Afrobeats")
        {
            p.targetLufs = -9.0;  p.lowDb = 2.0;  p.midDb = 1.0;  p.highDb = 0.5; p.vocalDb = 1.5;
        }
        else if (genreName == "Rock")
        {
            p.targetLufs = -10.0; p.lowDb = 0.0;  p.midDb = 1.0;  p.highDb = 0.5; p.vocalDb = 1.0;
        }
        else if (genreName == "Electronic / EDM")
        {
            p.targetLufs = -8.0;  p.lowDb = 2.0;  p.midDb = -1.0; p.highDb = 1.5; p.vocalDb = 0.0;
        }
        else if (genreName == "Podcast")
        {
            p.targetLufs = -16.0; p.lowDb = -2.0; p.midDb = 1.0;  p.highDb = 0.0; p.vocalDb = 3.0;
        }
        else
        {
            p.name.clear();   // unknown genre → invalid profile
        }
        return p;
    }

    static juce::StringArray genreNames()
    {
        return { "Hip-Hop / Rap", "Trap / Phonk", "R&B", "Pop", "Afrobeats",
                 "Rock", "Electronic / EDM", "Podcast" };
    }
};
