#include "IntentEngine.h"
#include "engine/MixerEngine.h"

// Heuristic rule values (Phase 2). Sensible pro defaults — intended to be refined by
// the mix engineer. The interface is stable so Phase 3 can swap ML in behind it.
namespace
{
    using Cmd = AICommandBridge::Command;

    // ---- level / pan (no nested transaction — the bridge owns it) ----
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

    // ---- native plugin insertion + param config ----
    Cmd eqWarmCmd (te::AudioTrack* t)
    {
        return [t] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            auto ptr = t->edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
            if (auto* eq = dynamic_cast<te::EqualiserPlugin*> (ptr.get()))
            {
                t->pluginList.insertPlugin (ptr, -1, nullptr);
                eq->loFreqValue.setValue (200.0f, &um);
                eq->loGainValue.setValue (2.0f,   &um);   // +2 dB low shelf — warmth
                eq->loQValue.setValue   (0.7f,    &um);
            }
        };
    }

    Cmd eqClearCmd (te::AudioTrack* t)
    {
        return [t] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            auto ptr = t->edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
            if (auto* eq = dynamic_cast<te::EqualiserPlugin*> (ptr.get()))
            {
                t->pluginList.insertPlugin (ptr, -1, nullptr);
                eq->midFreqValue1.setValue (3000.0f, &um);
                eq->midGainValue1.setValue (2.5f,    &um);   // presence
                eq->midQValue1.setValue    (1.0f,    &um);
                eq->hiFreqValue.setValue   (8000.0f, &um);
                eq->hiGainValue.setValue   (1.5f,    &um);   // air
            }
        };
    }

    Cmd compCmd (te::AudioTrack* t, float ratio, float thresholdGain, float attackMs, float releaseMs)
    {
        return [t, ratio, thresholdGain, attackMs, releaseMs] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            auto ptr = t->edit.getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
            if (auto* c = dynamic_cast<te::CompressorPlugin*> (ptr.get()))
            {
                t->pluginList.insertPlugin (ptr, -1, nullptr);
                c->ratio.getCachedValue().setValue          (ratio,         &um);
                c->thresholdGain.getCachedValue().setValue  (thresholdGain, &um);
                c->attackMs.getCachedValue().setValue       (attackMs,      &um);
                c->releaseMs.getCachedValue().setValue      (releaseMs,     &um);
            }
        };
    }

    Cmd reverbCmd (te::AudioTrack* t, float wet, float room, float width)
    {
        return [t, wet, room, width] (juce::UndoManager& um)
        {
            if (t == nullptr) return;
            auto ptr = t->edit.getPluginCache().createNewPlugin (te::ReverbPlugin::xmlTypeName, {});
            if (auto* r = dynamic_cast<te::ReverbPlugin*> (ptr.get()))
            {
                t->pluginList.insertPlugin (ptr, -1, nullptr);
                r->wetValue.setValue      (wet,   &um);
                r->roomSizeValue.setValue (room,  &um);
                r->widthValue.setValue    (width, &um);
                r->dryValue.setValue      (0.9f,  &um);
            }
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
        return [em] (juce::UndoManager& um)
        {
            auto* edit = em->getEdit();
            if (edit == nullptr) return;
            auto ptr = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
            if (auto* c = dynamic_cast<te::CompressorPlugin*> (ptr.get()))
            {
                edit->getMasterPluginList().insertPlugin (ptr, -1, nullptr);
                c->ratio.getCachedValue().setValue         (2.0f,  &um);   // gentle 2:1 glue
                c->thresholdGain.getCachedValue().setValue (0.5f,  &um);
                c->attackMs.getCachedValue().setValue      (30.0f, &um);
                c->releaseMs.getCachedValue().setValue     (150.0f,&um);
            }
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
            p.commands.push_back (compCmd (t, 3.0f, 0.3f, 5.0f, 80.0f));
            p.description = "Mehr Punch: " + tn;
            p.mechanic    = "Kompressor 3:1, schneller Attack (5 ms) auf " + tn + ".";
            break;

        case Intent::MoreReverb:
            p.commands.push_back (reverbCmd (t, 0.20f, 0.6f, 1.0f));
            p.description = "Mehr Raum: " + tn;
            p.mechanic    = "Reverb: 20% wet, Room 0.6 auf " + tn + ".";
            break;
        case Intent::Wider:
            p.commands.push_back (reverbCmd (t, 0.12f, 0.5f, 1.0f));
            p.description = "Breiter: " + tn;
            p.mechanic    = "Reverb: volle Breite, 12% wet auf " + tn + ".";
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
