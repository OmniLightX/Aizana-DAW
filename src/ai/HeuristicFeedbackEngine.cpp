#include "HeuristicFeedbackEngine.h"
#include "audio/AudioEngine.h"

std::vector<HeuristicFeedbackEngine::Feedback>
HeuristicFeedbackEngine::evaluate (const MeterSnapshot& m)
{
    std::vector<Feedback> out;

    // Effectively silent — nothing to analyse yet.
    if (m.lufsIntegrated <= kSilenceLufs)
    {
        out.push_back ({ "Noch nichts zu hoeren - druecke Play oder nimm etwas auf." });
        return out;
    }

    // Clipping risk.
    if (m.peakDb > kNearClipDb)
        out.push_back ({ "Du bist nah am Clipping - ein Glue-Kompressor faengt die Spitzen ab.",
                         IntentEngine::Intent::GlueMix, true });

    // Quieter than competitive Pop.
    if (m.lufsIntegrated < kQuietLufs)
        out.push_back ({ "Dein Mix ist leiser als typische Pop-Songs (Ziel -9 bis -11 LUFS).",
                         IntentEngine::Intent::MatchPopReference, true });

    // Very dynamic — glue helps it sit together.
    if ((m.peakDb - m.rmsDb) > kHighCrestDb)
        out.push_back ({ "Sehr dynamisch - ein Glue-Kompressor verbindet den Mix.",
                         IntentEngine::Intent::GlueMix, true });

    // Nothing flagged -> positive reassurance.
    if (out.empty())
        out.push_back ({ "Dein Mix sitzt gut - schoener Pegel und genug Headroom." });

    if (out.size() > 3)
        out.resize (3);

    return out;
}

HeuristicFeedbackEngine::MeterSnapshot HeuristicFeedbackEngine::snapshot() const
{
    auto& m = audioEngine.getMasterMeterProcessor();
    return { m.getPeakDB(), m.getRMSDB(), m.getLUFSMomentary(), m.getLUFSIntegrated() };
}
