#include "MixDataLogger.h"
#include "MixFeatures.h"

namespace aizana
{
    namespace
    {
        const char* sideToStr (StemSide s)
        {
            switch (s)
            {
                case StemSide::Left:  return "L";
                case StemSide::Right: return "R";
                case StemSide::None:
                default:              return "none";
            }
        }
    }

    juce::File MixDataLogger::defaultLogFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Aizana/training/mix_decisions.jsonl");
    }

    juce::String MixDataLogger::signature (const MixAction& a)
    {
        // Stable enough to match an accepted action back to its proposed twin (params are
        // verbatim copies), with params in the key so serial moves on one track don't collide.
        return a.name() + "|" + a.targetName + "|"
             + juce::String (a.gainDb, 2)      + "|" + juce::String (a.freqHz, 1) + "|"
             + juce::String (a.thresholdDb, 2) + "|" + juce::String (a.ratio, 2) + "|"
             + juce::String (a.pan, 3);
    }

    const TrackMixProfile* MixDataLogger::findProfile (const std::vector<TrackMixProfile>& profiles,
                                                       const MixAction& a)
    {
        for (const auto& p : profiles)
            if (p.id == a.targetTrack)
                return &p;
        return nullptr;
    }

    juce::String MixDataLogger::toJsonLine (const TrackMixProfile& t,
                                            const MixAction& a,
                                            bool accepted,
                                            const MixLogContext& ctx)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("ts", ctx.timestamp.isNotEmpty()
                                     ? ctx.timestamp
                                     : juce::Time::getCurrentTime().toISO8601 (true));
        root->setProperty ("proposer", ctx.proposer);
        root->setProperty ("mode",     ctx.mode);
        if (ctx.genre.isNotEmpty())
            root->setProperty ("genre", ctx.genre);
        root->setProperty ("accepted", accepted);

        auto* tr = new juce::DynamicObject();
        tr->setProperty ("name",       t.name);
        tr->setProperty ("group",      t.group);
        tr->setProperty ("instrument", t.instrument);
        tr->setProperty ("subRole",    t.subRole);
        tr->setProperty ("side",       sideToStr (t.side));
        tr->setProperty ("rmsDb",      t.rmsDb);
        tr->setProperty ("peakDb",     t.peakDb);
        tr->setProperty ("pan",        t.currentPan);
        tr->setProperty ("lead",       t.isLeadVocal);
        root->setProperty ("track", juce::var (tr));

        // The exact model-input contract snapshot (MixFeatures.h) — what 5b trains on.
        juce::Array<juce::var> feat;
        for (float f : mixfeatures::buildGainTrim (t))
            feat.add (f);
        auto* features = new juce::DynamicObject();
        features->setProperty ("gain_trim", feat);
        root->setProperty ("features", juce::var (features));

        auto* ac = new juce::DynamicObject();
        ac->setProperty ("name",         a.name());
        ac->setProperty ("gainDb",       a.gainDb);
        ac->setProperty ("pan",          a.pan);
        ac->setProperty ("freqHz",       a.freqHz);
        ac->setProperty ("q",            a.q);
        ac->setProperty ("thresholdDb",  a.thresholdDb);
        ac->setProperty ("ratio",        a.ratio);
        ac->setProperty ("makeupGainDb", a.makeupGainDb);
        ac->setProperty ("reason",       a.reason);
        root->setProperty ("action", juce::var (ac));

        return juce::JSON::toString (juce::var (root), true);   // true = single line (JSONL)
    }

    int MixDataLogger::logReview (const std::vector<TrackMixProfile>& profiles,
                                  const MixPlan& proposed,
                                  const MixPlan& accepted,
                                  MixLogContext ctx,
                                  const juce::File& logFile)
    {
        if (proposed.actions.empty())
            return 0;

        if (ctx.timestamp.isEmpty())
            ctx.timestamp = juce::Time::getCurrentTime().toISO8601 (true);

        // Multiset of accepted signatures; consume on match so duplicates verdict correctly.
        std::vector<juce::String> acceptedSigs;
        acceptedSigs.reserve (accepted.actions.size());
        for (const auto& a : accepted.actions)
            acceptedSigs.push_back (signature (a));

        juce::StringArray lines;
        for (const auto& a : proposed.actions)
        {
            const TrackMixProfile* t = findProfile (profiles, a);
            if (t == nullptr)
                continue;   // can't describe the track's features → skip (no half examples)

            bool acc = false;
            const auto sig = signature (a);
            for (auto& s : acceptedSigs)
                if (s == sig) { acc = true; s.clear(); break; }   // consume one match

            lines.add (toJsonLine (*t, a, acc, ctx));
        }

        if (lines.isEmpty())
            return 0;

        logFile.getParentDirectory().createDirectory();
        if (! logFile.appendText (lines.joinIntoString ("\n") + "\n"))
            return 0;   // never disrupt the mix on a logging failure

        return lines.size();
    }
}
