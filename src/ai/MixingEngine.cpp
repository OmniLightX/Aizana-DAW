#include "MixingEngine.h"
#include "MixFeatures.h"

namespace aizana
{
    using Role = MixingEngine::Role;

    //==========================================================================
    // Feature contract for the local ML backend (see MixFeatures.h).
    std::vector<float> mixfeatures::buildGainTrim (const TrackMixProfile& t)
    {
        const auto norm = [] (double db) { return (float) juce::jlimit (0.0, 1.0, (db + 60.0) / 60.0); };

        std::vector<float> f ((size_t) GainTrimCount, 0.0f);
        f[RmsNorm]  = norm (t.rmsDb);
        f[PeakNorm] = norm (t.peakDb);
        f[IsVocal]  = (t.group == "Vocals") ? 1.0f : 0.0f;
        f[IsDrums]  = (t.group == "Drums")  ? 1.0f : 0.0f;
        f[IsBass]   = (t.group == "Bass")   ? 1.0f : 0.0f;
        f[IsLead]   = t.isLeadVocal ? 1.0f : 0.0f;
        return f;
    }

    //==========================================================================
    namespace
    {
        // A small action factory keeps the rule code below readable.
        MixAction make (MixActionType type, const TrackMixProfile& t, juce::String reason)
        {
            MixAction a;
            a.type        = type;
            a.targetTrack = t.id;
            a.targetName  = t.name;
            a.reason      = std::move (reason);
            return a;
        }

        // const char* overload — string literals in this build are NOT decoded as UTF-8
        // by juce::String(const char*) (umlauts mojibake to "Ã¤"). All literal reasons
        // funnel through here, so decoding once with fromUTF8 fixes the whole engine.
        MixAction make (MixActionType type, const TrackMixProfile& t, const char* reason)
        {
            return make (type, t, juce::String::fromUTF8 (reason));
        }

        double clampGain (double db)
        {
            return juce::jlimit ((double) MixingEngine::kMinGainDb,
                                 (double) MixingEngine::kMaxGainDb, db);
        }

        // Reference nudge, always small and clamped (REF-04: a hint, not a target).
        double nudged (double base, double ref, double scale, double lo, double hi)
        {
            return juce::jlimit (lo, hi, base + ref * scale);
        }

        // Role-dependent low-cut corner; 0 = no high-pass (kick/808 live here).
        float highPassHzFor (Role r)
        {
            switch (r)
            {
                case Role::Vocals:      return 80.0f;
                case Role::Keys:        return 90.0f;
                case Role::Instruments: return 100.0f;
                case Role::Guitars:     return 100.0f;
                case Role::Synths:      return 120.0f;
                case Role::FX:          return 120.0f;
                case Role::Drums:       return 0.0f;   // full kit / kick — leave the bottom
                case Role::Bass:        return 0.0f;
                case Role::Other:       return 0.0f;
            }
            return 0.0f;
        }

        bool wantsMudCarve (Role r)
        {
            return r == Role::Keys || r == Role::Guitars || r == Role::Synths
                || r == Role::Vocals || r == Role::Instruments;
        }

        // Pan width for the spread depends on the genre feel of the reference.
        float spreadWidthFor (const ReferenceProfile* ref)
        {
            if (ref == nullptr || ! ref->isValid())
                return 0.40f;

            const auto& g = ref->name;
            if (g == "Rock" || g == "Electronic / EDM")          return 0.50f;  // wide
            if (g == "Hip-Hop / Rap" || g == "Trap / Phonk")     return 0.28f;  // tight, mono-ish low end
            if (g == "Podcast")                                   return 0.0f;   // mono-safe
            return 0.40f;
        }

        bool centreLocked (Role r, const TrackMixProfile& t)
        {
            return r == Role::Bass || r == Role::Drums || t.isLeadVocal;
        }

        // How far under the lead a supporting vocal sits (dB, negative).
        double vocalSupportOffsetDb (const juce::String& subRole)
        {
            if (subRole == "Choir")    return -5.0;
            if (subRole == "Backing")  return -4.0;
            if (subRole == "Harmony")  return -4.0;
            if (subRole == "Adlib")    return -3.0;
            if (subRole == "Double")   return -2.0;   // doubles sit close, just spread
            if (subRole == "Shout")    return -1.0;   // shouts are meant to cut through
            return -3.0;                              // any other non-lead vocal
        }

        // Hard for doubles (tight stereo widening), moderate for the rest.
        float sidePanAmount (const juce::String& subRole)
        {
            return subRole == "Double" ? 0.85f : 0.55f;
        }

        float sideToPan (StemSide s, float amount)
        {
            return s == StemSide::Left ? -amount : (s == StemSide::Right ? amount : 0.0f);
        }

        //----------------------------------------------------------------------
        // Drum-element-specific mixing (the kit is not one thing — Grammy practice).
        enum class DrumElement { Kick, Snare, HiHat, CymbalOH, Tom, Clap, Perc, Generic };

        DrumElement drumElementFor (const juce::String& instrument)
        {
            const auto n = instrument.toLowerCase();
            if (n.contains ("kick"))                                         return DrumElement::Kick;
            if (n.contains ("snare") || n.contains ("rim") || n.contains ("stick"))
                                                                            return DrumElement::Snare;
            if (n.contains ("hat") || n.contains ("hihat") || n == "hh")     return DrumElement::HiHat;
            if (n.contains ("crash") || n.contains ("ride") || n.contains ("splash")
                || n.contains ("china") || n.contains ("cymbal") || n.contains ("overhead"))
                                                                            return DrumElement::CymbalOH;
            if (n.contains ("tom"))                                          return DrumElement::Tom;
            if (n.contains ("clap") || n.contains ("snap"))                  return DrumElement::Clap;
            if (n.contains ("perc")  || n.contains ("conga") || n.contains ("bongo")
                || n.contains ("timbale") || n.contains ("cowbell") || n.contains ("shaker")
                || n.contains ("tamb") || n.contains ("clave") || n.contains ("triangle")
                || n.contains ("djembe") || n.contains ("cajon") || n.contains ("woodblock")
                || n.contains ("cabasa") || n.contains ("tabla"))           return DrumElement::Perc;
            return DrumElement::Generic;
        }

        // Appends the element's HPF + EQ + compression to the plan (Grammy-informed).
        void addDrumElementMoves (MixPlan& plan, const TrackMixProfile& t)
        {
            auto eq = [&] (MixActionType type, float hz, float gain, float q, const char* why)
            {
                auto a = make (type, t, why);
                a.freqHz = hz; a.gainDb = gain; a.q = q;
                plan.add (a);
            };
            auto hpf = [&] (float hz, const char* why)
            {
                auto a = make (MixActionType::HighPassFilter, t, why);
                a.freqHz = hz;
                plan.add (a);
            };
            auto comp = [&] (float ratio, float thr, float makeup, const char* why)
            {
                auto a = make (MixActionType::Compress, t, why);
                a.ratio = ratio; a.thresholdDb = thr; a.makeupGainDb = makeup;
                plan.add (a);
            };

            switch (drumElementFor (t.instrument))
            {
                case DrumElement::Kick:
                    // Heartbeat: keep the bottom, clean the box, add beater click.
                    eq (MixActionType::LowShelf,  60.0f,  2.0f, 0.7f, "Mehr Wucht im Kick-Fundament");
                    eq (MixActionType::CarveBand, 350.0f, -2.5f, 1.0f, "Nimmt das Pappige aus dem Kick");
                    eq (MixActionType::BoostBand, 4000.0f, 2.0f, 1.0f, "Beater-Click, damit der Kick durchkommt");
                    comp (4.0f, -14.0f, 1.0f, "Kick bleibt konstant und punchy");
                    break;
                case DrumElement::Snare:
                    hpf (90.0f, "Tiefen-Rumpeln raus");
                    eq (MixActionType::BoostBand, 200.0f, 2.0f, 1.0f, "Body in die Snare");
                    eq (MixActionType::HighShelf, 6000.0f, 2.0f, 0.7f, "Crispness oben drauf");
                    comp (4.0f, -16.0f, 2.0f, "Snare poppt und sitzt");
                    break;
                case DrumElement::HiHat:
                    // High-passed hard — the hat adds crispness, not low end. No comp.
                    hpf (400.0f, "HiHat hoch beschnitten — nur Glanz, kein Tiefton");
                    eq (MixActionType::HighShelf, 9000.0f, 1.5f, 0.7f, "Mehr Crispness in der HiHat");
                    break;
                case DrumElement::CymbalOH:
                    hpf (350.0f, "Bleed/Tiefton raus, Becken bleiben luftig");
                    eq (MixActionType::HighShelf, 10000.0f, 1.0f, 0.7f, "Luft in den Becken");
                    break;
                case DrumElement::Tom:
                    hpf (80.0f, "Sub-Rumpeln raus");
                    eq (MixActionType::CarveBand, 400.0f, -2.0f, 1.0f, "Mulm aus den Toms");
                    eq (MixActionType::BoostBand, 5000.0f, 2.0f, 1.0f, "Attack der Toms");
                    comp (3.0f, -16.0f, 1.0f, "Toms gleichmäßiger");
                    break;
                case DrumElement::Clap:
                    hpf (150.0f, "Tiefen raus");
                    eq (MixActionType::BoostBand, 4000.0f, 2.0f, 1.0f, "Snap nach vorn");
                    comp (3.0f, -16.0f, 1.0f, "Claps konstant");
                    break;
                case DrumElement::Perc:
                    hpf (150.0f, "Tiefen raus, Percussion bleibt sauber");
                    comp (2.5f, -16.0f, 0.5f, "Percussion gleichmäßig");
                    break;
                case DrumElement::Generic:
                    comp (4.0f, -12.0f, 2.0f, "Drums bekommen Punch und Druck");
                    break;
            }
        }
    }

    //==========================================================================
    Role MixingEngine::roleForGroup (const juce::String& group)
    {
        const auto g = group.trim();
        if (g == "Drums")       return Role::Drums;
        if (g == "Bass")        return Role::Bass;
        if (g == "Guitars")     return Role::Guitars;
        if (g == "Keys")        return Role::Keys;
        if (g == "Synths")      return Role::Synths;
        if (g == "Instruments") return Role::Instruments;
        if (g == "Vocals")      return Role::Vocals;
        if (g == "FX")          return Role::FX;
        return Role::Other;
    }

    //==========================================================================
    MixPlan MixingEngine::mix (const std::vector<TrackMixProfile>& tracks,
                               const ReferenceProfile* reference,
                               const MixOptions& options,
                               IInferenceBackend* backend)
    {
        const bool useMl = (backend != nullptr && backend->isAvailable());

        MixPlan plan;
        if (tracks.empty())
            return plan;   // nothing to mix → no plan (and no empty-master glue)

        const double lowRef   = reference ? reference->lowDb   : 0.0;
        const double midRef   = reference ? reference->midDb   : 0.0;
        const double highRef  = reference ? reference->highDb  : 0.0;
        const double vocalRef = reference ? reference->vocalDb : 0.0;

        //----------------------------------------------------------------------
        // 1) Per-track moves: level → EQ (subtractive first) → dynamics.
        for (const auto& t : tracks)
        {
            const Role role = roleForGroup (t.group);

            // --- Level / gain staging (skip silent or already-balanced tracks) ---
            if (t.rmsDb > kSilenceRmsDb)
            {
                double target = (role == Role::Vocals || t.isLeadVocal)
                                  ? nudged (kVocalRmsDb, vocalRef, 0.5, kVocalRmsDb, -13.0)
                                  : kDefaultRmsDb;
                if (role == Role::Vocals && ! t.isLeadVocal)
                    target += vocalSupportOffsetDb (t.subRole);   // backings/doubles sit under the lead
                if (role == Role::Bass)
                    target = nudged (kDefaultRmsDb, lowRef, 0.4, -20.0, -16.0);

                const double gain = clampGain (target - t.rmsDb);
                if (std::abs (gain) >= kMinGainMoveDb)
                {
                    // Heuristic decides WHETHER to move; the ML backend (when available)
                    // only refines the magnitude — clamped hard and folded into the same
                    // action, so an unavailable/misbehaving model degrades to the heuristic.
                    double finalGain = gain;
                    if (useMl)
                    {
                        const auto out = backend->run (mixfeatures::buildGainTrim (t));
                        if (! out.empty())
                        {
                            const double trim = juce::jlimit (-(double) kMlTrimClampDb,
                                                              (double) kMlTrimClampDb,
                                                              (double) out[0]);
                            finalGain = clampGain (gain + trim);
                        }
                    }

                    auto a = make (MixActionType::SetTrackGain, t,
                                   gain >= 0.0 ? "Lauter, damit die Spur im Mix sitzt"
                                               : "Leiser, macht Platz für die anderen");
                    a.gainDb = (float) finalGain;
                    plan.add (a);
                }
            }

            // --- Low cut: rumble out of everything that isn't bass/kick ---
            if (const float hp = highPassHzFor (role); hp > 0.0f)
            {
                auto a = make (MixActionType::HighPassFilter, t,
                               "Tiefen-Rumpeln raus, hält den Bassbereich sauber");
                a.freqHz = hp;
                plan.add (a);
            }

            // --- Bass: shape the low end with a shelf, driven by genre emphasis ---
            if (role == Role::Bass)
            {
                const double shelf = juce::jlimit (-3.0, 3.0, lowRef);
                if (std::abs (shelf) >= 0.5)
                {
                    auto a = make (MixActionType::LowShelf, t,
                                   shelf >= 0.0 ? "Mehr Fundament im Bass"
                                                : "Weniger Wummern im Bass");
                    a.freqHz = 90.0f;
                    a.gainDb = (float) shelf;
                    a.q      = 0.7f;
                    plan.add (a);
                }
            }

            // --- Mud carve: subtractive cleanup of the boxy low-mids ---
            if (wantsMudCarve (role))
            {
                auto a = make (MixActionType::CarveBand, t,
                               "Nimmt das Dumpfe um 300 Hz weg, der Mix wird klarer");
                a.freqHz = kMudFreqHz;
                a.gainDb = (float) juce::jlimit (-4.0, -1.5, kMudCutDb + midRef * 0.5);
                a.q      = kMudQ;
                plan.add (a);
            }

            // --- Lead vocal: presence + air + de-ess + serial compression ---
            if (t.isLeadVocal)
            {
                auto presence = make (MixActionType::BoostBand, t,
                                      "Holt die Stimme nach vorn, mehr Verständlichkeit");
                presence.freqHz = kVocalPresenceHz;
                presence.gainDb = (float) nudged (kVocalPresenceDb, highRef, 0.3, 1.5, 3.0);
                presence.q      = kVocalPresenceQ;
                plan.add (presence);

                auto air = make (MixActionType::HighShelf, t,
                                 "Öffnet die Höhen, mehr Luft und Glanz");
                air.freqHz = kVocalAirHz;
                air.gainDb = (float) nudged (kVocalAirDb, highRef, 0.3, 0.5, 3.0);
                air.q      = 0.7f;
                plan.add (air);

                auto deess = make (MixActionType::DeEss, t,
                                   "Zähmt scharfe S-Laute (Zischen)");
                deess.freqHz      = kDeEssHz;
                deess.thresholdDb = kDeEssThreshDb;
                deess.ratio       = kDeEssRatio;
                plan.add (deess);

                // Serial-lite (user decision): a gentle leveller, then a peak catcher
                // — the Grammy-standard two-stage vocal chain (LA-2A → 1176).
                auto level = make (MixActionType::Compress, t,
                                   "Stufe 1: hält die Lautstärke der Stimme konstant");
                level.ratio        = 2.5f;
                level.thresholdDb  = -22.0f;
                level.makeupGainDb = 0.0f;
                plan.add (level);

                auto peak = make (MixActionType::Compress, t,
                                  "Stufe 2: fängt die lautesten Spitzen ab");
                peak.ratio        = 4.0f;
                peak.thresholdDb  = kVocalCompThreshDb;
                peak.makeupGainDb = 2.0f;
                plan.add (peak);
            }
            // --- Supporting vocals (backings / doubles / adlibs / shouts / harmonies) ---
            else if (role == Role::Vocals)
            {
                // Sibilance lives on every vocal layer, not just the lead.
                auto deess = make (MixActionType::DeEss, t,
                                   juce::String::fromUTF8 ("Zähmt scharfe S-Laute der ") + t.subRole + "-Stimme");
                deess.freqHz      = kDeEssHz;
                deess.thresholdDb = kDeEssThreshDb;
                deess.ratio       = kDeEssRatio;
                plan.add (deess);

                // One gentle leveller — keep the layer steady and tucked under the lead.
                auto comp = make (MixActionType::Compress, t,
                                  "Hält die Stützstimme gleichmäßig unter dem Lead");
                comp.ratio        = 3.0f;
                comp.thresholdDb  = -20.0f;
                comp.makeupGainDb = 0.0f;
                plan.add (comp);

                // Pro bussing: backings/harmonies/doubles on a "Backings" sub-bus,
                // adlibs/shouts on an "Adlibs" sub-bus (each gets bus glue on apply).
                const bool isAdlibLayer = (t.subRole == "Adlib" || t.subRole == "Shout");
                MixRouting r;
                r.trackId   = t.id;
                r.trackName = t.name;
                r.busName   = isAdlibLayer ? "Adlibs" : "Backings";
                r.reason    = isAdlibLayer ? "Adlibs auf eigenen Bus (wie die Profis)"
                                           : "Backings auf eigenen Bus, zusammen verklebt";
                plan.route (r);
            }

            // --- Role compressors (bass steadiness, drum punch) ---
            if (role == Role::Bass)
            {
                auto a = make (MixActionType::Compress, t, "Bass bleibt gleichmäßig laut");
                a.ratio        = 2.5f;
                a.thresholdDb  = -18.0f;
                a.makeupGainDb = 1.0f;
                plan.add (a);
            }
            else if (role == Role::Drums)
            {
                // Element-specific: kick ≠ hihat ≠ snare ≠ cymbal (HPF + EQ + comp).
                addDrumElementMoves (plan, t);
            }
        }

        //----------------------------------------------------------------------
        // 2) Panning pass: anchors stay centre, the rest spreads round-robin.
        const float width = spreadWidthFor (reference);
        int spreadIndex = 0;
        for (const auto& t : tracks)
        {
            const Role role = roleForGroup (t.group);

            if (centreLocked (role, t))
            {
                if (std::abs (t.currentPan) > 0.05f)
                {
                    auto a = make (MixActionType::SetTrackPan, t,
                                   "In die Mitte — trägt den Mix");
                    a.pan = 0.0f;
                    plan.add (a);
                }
                continue;
            }

            // Explicit L/R in the name (Doubles L, Gtr R, Backings Rechts) → hard placement.
            if (t.side != StemSide::None && width > 0.0f)
            {
                const float target = sideToPan (t.side, sidePanAmount (t.subRole));
                if (std::abs (target - t.currentPan) > 0.05f)
                {
                    auto a = make (MixActionType::SetTrackPan, t,
                                   juce::String (t.side == StemSide::Left ? "Nach links" : "Nach rechts")
                                       + " gepannt (Stereo-Paar)");
                    a.pan = target;
                    plan.add (a);
                }
                continue;
            }

            if (width <= 0.0f)
                continue;   // mono-safe genre (e.g. podcast)

            // Alternate L / R, widening with each pair.
            const float side   = (spreadIndex % 2 == 0) ? -1.0f : 1.0f;
            const float amount = juce::jlimit (0.0f, 0.85f, width + 0.08f * (spreadIndex / 2));
            const float target = side * amount;
            ++spreadIndex;

            if (std::abs (target - t.currentPan) > 0.05f)
            {
                auto a = make (MixActionType::SetTrackPan, t,
                               "Ins Stereobild verteilt, mehr Breite");
                a.pan = target;
                plan.add (a);
            }
        }

        //----------------------------------------------------------------------
        // 3) Master glue — gentle, the "mix into the bus" move (reviewable).
        if (options.allowMasterGlue)
        {
            MixAction g;
            g.type         = MixActionType::Compress;
            g.targetTrack  = options.masterId;
            g.targetName   = options.masterName;
            g.ratio        = kGlueRatio;
            g.thresholdDb  = kGlueThreshDb;
            g.makeupGainDb = kGlueMakeupDb;
            g.reason       = juce::String::fromUTF8 ("Klebt den ganzen Mix zusammen — sanft, wie die Profis in den Bus mixen");
            plan.add (g);
        }

        //----------------------------------------------------------------------
        // 4) REF-03 — style match: tilt the master toward the reference's tonal balance.
        //    A gentle finishing shelf pair (the per-track moves already do the heavy lifting),
        //    clamped to kRefMasterTiltMaxDb. REF-04: a guideline read from the reference, not
        //    an enforced target. targetName = master marker so MixApplyEngine routes it to the
        //    master plugin list (same convention as the glue compressor).
        if (reference != nullptr && reference->isValid())
        {
            auto masterShelf = [&] (MixActionType type, double refDb, float hz, const char* reasonUp, const char* reasonDown)
            {
                const double db = juce::jlimit (-MixingEngine::kRefMasterTiltMaxDb,
                                                 MixingEngine::kRefMasterTiltMaxDb,
                                                 refDb * MixingEngine::kRefMasterTiltScale);
                if (std::abs (db) < MixingEngine::kRefMasterTiltMinDb)
                    return;

                MixAction a;
                a.type        = type;
                a.targetTrack = options.masterId;
                a.targetName  = options.masterName;
                a.freqHz      = hz;
                a.gainDb      = (float) db;
                a.q           = 0.7f;
                a.reason      = juce::String::fromUTF8 (db >= 0.0 ? reasonUp : reasonDown);
                plan.add (a);
            };

            masterShelf (MixActionType::HighShelf, highRef, MixingEngine::kRefMasterHighHz,
                         "Etwas mehr Glanz oben \xe2\x80\x94 n\xc3\xa4her an deiner Referenz",
                         "Etwas weniger H\xc3\xb6hen \xe2\x80\x94 n\xc3\xa4her an deiner Referenz");
            masterShelf (MixActionType::LowShelf, lowRef, MixingEngine::kRefMasterLowHz,
                         "Etwas mehr Fundament unten \xe2\x80\x94 n\xc3\xa4her an deiner Referenz",
                         "Etwas weniger Bass \xe2\x80\x94 n\xc3\xa4her an deiner Referenz");
        }

        //----------------------------------------------------------------------
        juce::String genre = (reference && reference->isValid()) ? reference->name : juce::String();
        plan.summary = "Mix-Vorschlag: " + juce::String (plan.size())
                     + juce::String::fromUTF8 (" Schritte über ")
                     + juce::String ((int) tracks.size()) + " Spuren"
                     + (genre.isNotEmpty() ? " (Stil: " + genre + ")" : juce::String());
        return plan;
    }
}
