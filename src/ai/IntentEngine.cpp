#include "IntentEngine.h"
#include "engine/MixerEngine.h"
#include "plugins/BuiltInPluginFormat.h"
#include "fx/EQProcessor.h"

// Heuristic rule values (Phase 2). Sensible pro defaults — intended to be refined by
// the mix engineer. The interface is stable so Phase 3 can swap ML in behind it.
//
// IMPORTANT (Fix 3, 2026-06-05): character/space/glue intents insert *Cloudes'* own
// built-in FX (EQProcessor / CompressorProcessor / ReverbProcessor) — NOT the native
// te::EqualiserPlugin/CompressorPlugin/ReverbPlugin. Cloudes only ships editors for its
// own processors (see FloatingPluginWindow.cpp Stage 2); the native TE plugins render
// "No editor available", which breaks the Nordstern ("editable mechanic"). The Cloudes
// processors are inserted through TE's ExternalPlugin via BuiltInPluginFormat — the
// exact path the mixer's insert slots use.
namespace
{
    using Cmd = AICommandBridge::Command;

    // ---- helpers -----------------------------------------------------------

    // Set an APVTS-backed host parameter (by paramID) to a real (un-normalised) value.
    // setValueNotifyingHost drives the AudioProcessorValueTreeState listener, so BOTH
    // the audio-thread atomics AND the APVTS state tree (what the generic editor shows)
    // stay in sync — set the atomic alone (setThreshold/…) would leave the editor stale.
    void setHostParam (juce::AudioPluginInstance& inst, const juce::String& id, float realValue)
    {
        for (auto* p : inst.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID == id)
                {
                    rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
                    return;
                }
    }

    // Create a Cloudes built-in FX (wrapped as an ExternalPlugin so it carries the
    // Cloudes editor), insert it at the end of `list`, fully initialise it, run
    // `configure` on the live instance, then flush state into the ValueTree so the
    // configuration survives undo/redo and session save. Insertion is recorded in the
    // edit's UndoManager (the bridge's active transaction) — one undo removes it whole.
    template <typename ConfigFn>
    void insertBuiltIn (te::Edit& edit, te::PluginList& list,
                        const juce::String& fxName, ConfigFn&& configure)
    {
        const auto desc = BuiltInPluginFormat::getDescriptionFor (fxName);
        auto ptr = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (ptr.get()))
        {
            list.insertPlugin (ptr, -1, nullptr);
            ext->initialiseFully();

            if (auto* inst = ext->getAudioPluginInstance())
                configure (*inst);

            ext->flushPluginStateToValueTree();
        }
    }

    // ---- level / pan (no plugin — the track's volume plugin) ----
    Cmd volumeDeltaCmd (te::AudioTrack* t, float deltaDb)
    {
        return [t, deltaDb] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            if (auto* vp = t->getVolumePlugin())
            {
                const float curDb = te::volumeFaderPositionToDB (vp->volume.get());
                const float pos   = te::decibelsToVolumeFaderPosition (curDb + deltaDb);
                vp->volume.setValue (pos, &um);
                vp->setSliderPos (pos);
            }
        };
    }

    Cmd panCmd (te::AudioTrack* t, float pan)
    {
        return [t, pan] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            if (auto* vp = t->getVolumePlugin())
            {
                vp->pan.setValue (pan, &um);
                vp->setPan (pan);
            }
        };
    }

    // ---- character: Cloudes EQ / Compressor ----
    Cmd eqWarmCmd (te::AudioTrack* t)
    {
        return [t] (juce::UndoManager&)
        {
            if (t == nullptr) return;
            insertBuiltIn (t->edit, t->pluginList, "Cloudes EQ", [] (juce::AudioPluginInstance& inst)
            {
                if (auto* eq = dynamic_cast<EQProcessor*> (&inst))
                {
                    EQBand low;
                    low.type        = EQBandType::LowShelf;
                    low.frequencyHz = 200.0f;
                    low.gainDB      = 2.0f;   // +2 dB low shelf — warmth
                    low.Q           = 0.7f;
                    low.enabled     = true;
                    eq->setBand (0, low);
                }
            });
        };
    }

    Cmd eqClearCmd (te::AudioTrack* t)
    {
        return [t] (juce::UndoManager&)
        {
            if (t == nullptr) return;
            insertBuiltIn (t->edit, t->pluginList, "Cloudes EQ", [] (juce::AudioPluginInstance& inst)
            {
                if (auto* eq = dynamic_cast<EQProcessor*> (&inst))
                {
                    EQBand presence;
                    presence.type        = EQBandType::Peak;
                    presence.frequencyHz = 3000.0f;
                    presence.gainDB      = 2.5f;   // presence
                    presence.Q           = 1.0f;
                    presence.enabled     = true;
                    eq->setBand (0, presence);

                    EQBand air;
                    air.type        = EQBandType::HighShelf;
                    air.frequencyHz = 8000.0f;
                    air.gainDB      = 1.5f;        // air
                    air.Q           = 0.7f;
                    air.enabled     = true;
                    eq->setBand (1, air);
                }
            });
        };
    }

    Cmd compCmd (te::AudioTrack* t, float ratio, float thresholdDb, float attackMs, float releaseMs)
    {
        return [t, ratio, thresholdDb, attackMs, releaseMs] (juce::UndoManager&)
        {
            if (t == nullptr) return;
            insertBuiltIn (t->edit, t->pluginList, "Cloudes Compressor",
                           [ratio, thresholdDb, attackMs, releaseMs] (juce::AudioPluginInstance& inst)
            {
                setHostParam (inst, "ratio",     ratio);
                setHostParam (inst, "threshold", thresholdDb);
                setHostParam (inst, "attack",    attackMs);
                setHostParam (inst, "release",   releaseMs);
            });
        };
    }

    // ---- space: Cloudes Reverb ----
    Cmd reverbCmd (te::AudioTrack* t, float wet, float room)
    {
        return [t, wet, room] (juce::UndoManager&)
        {
            if (t == nullptr) return;
            insertBuiltIn (t->edit, t->pluginList, "Cloudes Reverb",
                           [wet, room] (juce::AudioPluginInstance& inst)
            {
                setHostParam (inst, "wet",      wet);
                setHostParam (inst, "roomSize", room);
            });
        };
    }

    // ---- master-bus commands (loudness/glue) ----
    Cmd masterVolumeDeltaCmd (EditManager* em, float deltaDb)
    {
        return [em, deltaDb] (juce::UndoManager& um)
        {
            auto* edit = em->getEdit();
            if (edit == nullptr) return;
            if (auto mvp = edit->getMasterVolumePlugin())
            {
                const float curDb = te::volumeFaderPositionToDB (mvp->volume.get());
                const float pos   = te::decibelsToVolumeFaderPosition (curDb + deltaDb);
                mvp->volume.setValue (pos, &um);
                mvp->setSliderPos (pos);
            }
        };
    }

    Cmd masterGlueCmd (EditManager* em)
    {
        return [em] (juce::UndoManager&)
        {
            auto* edit = em->getEdit();
            if (edit == nullptr) return;
            insertBuiltIn (*edit, edit->getMasterPluginList(), "Cloudes Compressor",
                           [] (juce::AudioPluginInstance& inst)
            {
                setHostParam (inst, "ratio",     2.0f);    // gentle 2:1 glue
                setHostParam (inst, "threshold", -6.0f);
                setHostParam (inst, "attack",    30.0f);
                setHostParam (inst, "release",   150.0f);
            });
        };
    }
}

//==============================================================================
te::AudioTrack* IntentEngine::resolveTarget (te::AudioTrack* requested) const
{
    if (requested != nullptr)
        return requested;

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    for (auto* t : te::getAudioTracks (*edit))
        if (! MixerEngine::isBusChannel (*t) && ! MixerEngine::isFXChannel (*t))
            return t;

    return nullptr;
}

IntentEngine::Plan IntentEngine::plan (Intent intent, te::AudioTrack* requested) const
{
    Plan p;
    auto* em = const_cast<EditManager*> (&editManager);

    // ReferenceLoudness intents act on the master; the rest on a target track.
    const bool masterIntent = (category (intent) == Category::ReferenceLoudness);
    te::AudioTrack* t = masterIntent ? nullptr : resolveTarget (requested);
    const juce::String tn = (t != nullptr ? t->getName() : juce::String ("die Spur"));

    if (! masterIntent && t == nullptr)
        return p;   // nothing to act on

    switch (intent)
    {
        case Intent::VocalForward:
            p.commands.push_back (volumeDeltaCmd (t, 2.0f));
            p.description = "Nach vorne: " + tn;
            p.mechanic    = "Pegel: +2.0 dB auf " + tn + ".";
            break;
        case Intent::QuieterInMix:
            p.commands.push_back (volumeDeltaCmd (t, -2.0f));
            p.description = "Leiser: " + tn;
            p.mechanic    = "Pegel: -2.0 dB auf " + tn + ".";
            break;
        case Intent::CenterInMix:
            p.commands.push_back (panCmd (t, 0.0f));
            p.description = "Mittig: " + tn;
            p.mechanic    = "Pan: zentriert (" + tn + ").";
            break;

        case Intent::Warmer:
            p.commands.push_back (eqWarmCmd (t));
            p.description = "Waermer: " + tn;
            p.mechanic    = "EQ: Low-Shelf +2.0 dB @ 200 Hz auf " + tn + ".";
            break;
        case Intent::Clearer:
            p.commands.push_back (eqClearCmd (t));
            p.description = "Klarer: " + tn;
            p.mechanic    = "EQ: +2.5 dB @ 3 kHz (Praesenz), +1.5 dB @ 8 kHz (Air) auf " + tn + ".";
            break;
        case Intent::MorePunch:
            p.commands.push_back (compCmd (t, 3.0f, -12.0f, 5.0f, 80.0f));
            p.description = "Mehr Punch: " + tn;
            p.mechanic    = "Kompressor 3:1, Schwelle -12 dB, schneller Attack (5 ms) auf " + tn + ".";
            break;

        case Intent::MoreReverb:
            p.commands.push_back (reverbCmd (t, 0.20f, 0.6f));
            p.description = "Mehr Raum: " + tn;
            p.mechanic    = "Reverb: 20% wet, Room 0.6 auf " + tn + ".";
            break;
        case Intent::Wider:
            p.commands.push_back (reverbCmd (t, 0.12f, 0.5f));
            p.description = "Breiter: " + tn;
            p.mechanic    = "Reverb: 12% wet, Room 0.5 auf " + tn + ".";
            break;
        case Intent::Closer:
            p.commands.push_back (volumeDeltaCmd (t, 1.5f));
            p.description = "Naeher: " + tn;
            p.mechanic    = "Pegel: +1.5 dB (naeher) auf " + tn + ".";
            break;

        case Intent::Louder:
            p.commands.push_back (masterVolumeDeltaCmd (em, 2.0f));
            p.description = "Lauter (Master)";
            p.mechanic    = "Master-Pegel: +2.0 dB.";
            break;
        case Intent::GlueMix:
            p.commands.push_back (masterGlueCmd (em));
            p.description = "Mix verkleben";
            p.mechanic    = "Glue-Kompressor 2:1 auf dem Master-Bus.";
            break;
        case Intent::MatchPopReference:
            p.commands.push_back (masterGlueCmd (em));
            p.commands.push_back (masterVolumeDeltaCmd (em, 1.0f));
            p.description = "Wie Pop-Referenz";
            p.mechanic    = "Glue-Kompressor 2:1 + Master +1 dB. Ziel: -10 LUFS (Pop).";
            break;
    }

    p.valid = ! p.commands.empty();
    return p;
}

//==============================================================================
IntentEngine::Category IntentEngine::category (Intent i)
{
    switch (i)
    {
        case Intent::VocalForward: case Intent::QuieterInMix: case Intent::CenterInMix:
            return Category::LevelBalance;
        case Intent::Warmer: case Intent::Clearer: case Intent::MorePunch:
            return Category::Character;
        case Intent::MoreReverb: case Intent::Wider: case Intent::Closer:
            return Category::SpaceDepth;
        case Intent::Louder: case Intent::GlueMix: case Intent::MatchPopReference:
        default:
            return Category::ReferenceLoudness;
    }
}

juce::String IntentEngine::label (Intent i)
{
    switch (i)
    {
        case Intent::VocalForward:      return "Nach vorne holen";
        case Intent::QuieterInMix:      return "Leiser im Mix";
        case Intent::CenterInMix:       return "Mittig setzen";
        case Intent::Warmer:            return "Waermer";
        case Intent::Clearer:           return "Klarer";
        case Intent::MorePunch:         return "Mehr Punch";
        case Intent::MoreReverb:        return "Mehr Hall";
        case Intent::Wider:             return "Breiter";
        case Intent::Closer:            return "Naeher";
        case Intent::Louder:            return "Lauter";
        case Intent::GlueMix:           return "Mix verkleben";
        case Intent::MatchPopReference: return "Wie Pop-Referenz";
        default:                        return "?";
    }
}

juce::String IntentEngine::categoryLabel (Category c)
{
    switch (c)
    {
        case Category::LevelBalance:      return "Pegel & Balance";
        case Category::Character:         return "Charakter";
        case Category::SpaceDepth:        return "Raum & Tiefe";
        case Category::ReferenceLoudness: return "Referenz & Loudness";
        default:                          return "?";
    }
}

std::vector<IntentEngine::Intent> IntentEngine::intentsFor (Category c)
{
    switch (c)
    {
        case Category::LevelBalance:      return { Intent::VocalForward, Intent::QuieterInMix, Intent::CenterInMix };
        case Category::Character:         return { Intent::Warmer, Intent::Clearer, Intent::MorePunch };
        case Category::SpaceDepth:        return { Intent::MoreReverb, Intent::Wider, Intent::Closer };
        case Category::ReferenceLoudness: return { Intent::Louder, Intent::GlueMix, Intent::MatchPopReference };
        default:                          return {};
    }
}
