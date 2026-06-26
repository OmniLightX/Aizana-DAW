#include "MixApplyEngine.h"
#include "StemAnalyzer.h"
#include "../engine/MixerEngine.h"
#include "../plugins/BuiltInPluginFormat.h"
#include "../fx/EQProcessor.h"
#include "../fx/CompressorProcessor.h"
#include "../fx/DeEsserProcessor.h"

#include <map>
#include <vector>
#include <cmath>

using cloudes::MixAction;
using cloudes::MixActionType;
using cloudes::MixPlan;
using cloudes::TrackMixProfile;

namespace
{
    // Identifies the master-bus glue action (MixingEngine labels it "Master").
    const juce::String kMasterTarget { "Master" };

    //==========================================================================
    // Built-in FX plumbing — mirrors IntentEngine: set parameters via the host
    // RangedAudioParameter (keeps the generic editor + APVTS state in sync, not
    // just the audio-thread atomic) and flush state so it survives undo/redo.

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

    template <typename ConfigFn>
    void insertBuiltIn (te::Edit& edit, te::PluginList& list,
                        const juce::String& fxName, ConfigFn&& configure)
    {
        const auto desc = BuiltInPluginFormat::getDescriptionFor (fxName);
        auto ptr = edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

        if (auto* ext = dynamic_cast<te::ExternalPlugin*> (ptr.get()))
        {
            list.insertPlugin (ptr, -1, nullptr);   // append; undoable in the open txn
            ext->initialiseFully();

            if (auto* inst = ext->getAudioPluginInstance())
                configure (*inst);

            ext->flushPluginStateToValueTree();      // persist for undo/redo + save
        }
    }

    //==========================================================================
    // Track volume / pan — transaction-free cores (the bridge owns the txn).

    void setTrackGainNoTxn (te::AudioTrack& track, float dB, juce::UndoManager& um)
    {
        if (auto* vp = track.getVolumePlugin())
        {
            const float pos = te::decibelsToVolumeFaderPosition (dB);   // SetTrackGain = absolute fader
            vp->volume.setValue (pos, &um);
            vp->setSliderPos (pos);
        }
    }

    void setTrackPanNoTxn (te::AudioTrack& track, float pan, juce::UndoManager& um)
    {
        if (auto* vp = track.getVolumePlugin())
        {
            const float clamped = juce::jlimit (-1.0f, 1.0f, pan);
            vp->pan.setValue (clamped, &um);
            vp->setPan (clamped);
        }
    }

    //==========================================================================
    // MixAction EQ band → EQProcessor band.

    EQBand toEqBand (const MixAction& a)
    {
        EQBand b;
        b.frequencyHz = a.freqHz;
        b.gainDB      = a.gainDb;
        b.Q           = a.q;
        b.enabled     = true;

        switch (a.type)
        {
            case MixActionType::HighPassFilter: b.type = EQBandType::HighPass; b.Q = 0.707f; b.slopeOrder = 2; break;
            case MixActionType::LowShelf:       b.type = EQBandType::LowShelf;  break;
            case MixActionType::HighShelf:      b.type = EQBandType::HighShelf; break;
            case MixActionType::CarveBand:
            case MixActionType::BoostBand:
            default:                            b.type = EQBandType::Peak; break;
        }
        return b;
    }

    bool isEqAction (MixActionType t)
    {
        return t == MixActionType::HighPassFilter || t == MixActionType::CarveBand
            || t == MixActionType::BoostBand     || t == MixActionType::LowShelf
            || t == MixActionType::HighShelf;
    }

    //==========================================================================
    // Reader helpers.

    juce::File firstClipSource (te::AudioTrack& track)
    {
        for (auto* c : track.getClips())
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
            {
                auto f = wc->getSourceFileReference().getFile();
                if (f.existsAsFile())
                    return f;
            }
        return {};
    }

    struct Levels { double rmsDb = -120.0; double peakDb = -120.0; };

    Levels fileLevels (const juce::File& f)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (afm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0.0)
            return {};

        const int len = (int) juce::jmin ((juce::int64) r->lengthInSamples,
                                          (juce::int64) (r->sampleRate * 30.0));   // cap at 30s
        const int chans = (int) juce::jmax (1u, r->numChannels);
        juce::AudioBuffer<float> buf (chans, len);
        r->read (&buf, 0, len, 0, true, true);

        double sumSq = 0.0;
        float  peak  = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
            {
                peak  = std::max (peak, std::abs (d[i]));
                sumSq += (double) d[i] * d[i];
            }
        }

        const double n = (double) len * juce::jmax (1, buf.getNumChannels());
        const double rmsLin = n > 0.0 ? std::sqrt (sumSq / n) : 0.0;

        Levels lv;
        lv.rmsDb  = rmsLin > 1e-10 ? 20.0 * std::log10 (rmsLin) : -120.0;
        lv.peakDb = peak   > 1e-10f ? 20.0 * std::log10 (peak)  : -120.0;
        return lv;
    }
}

//==============================================================================
MixApplyEngine::MixApplyEngine (EditManager& em, MixerEngine& mixer, AICommandBridge& bridge)
    : editManager (em), mixerEngine (mixer), commandBridge (bridge)
{
}

//==============================================================================
std::vector<MixApplyEngine::TrackScan> MixApplyEngine::scanTracks() const
{
    std::vector<TrackScan> scan;

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return scan;

    for (auto* t : te::getAudioTracks (*edit))
    {
        if (! MixerEngine::isAudioChannel (*t))   // skip bus / FX channels
            continue;

        const auto src = firstClipSource (*t);
        if (! src.existsAsFile())                 // skip empty tracks — nothing to mix
            continue;

        TrackScan s;
        s.id     = t->itemID;
        s.name   = t->getName();
        s.source = src;
        if (auto* vp = t->getVolumePlugin())
            s.currentPan = vp->pan.get();

        scan.push_back (std::move (s));
    }

    return scan;
}

std::vector<TrackMixProfile> MixApplyEngine::profilesFromScan (const std::vector<TrackScan>& scan)
{
    std::vector<TrackMixProfile> out;
    out.reserve (scan.size());

    for (const auto& s : scan)
    {
        TrackMixProfile p;
        p.id         = s.id;
        p.name       = s.name;
        p.currentPan = s.currentPan;

        const auto c = StemAnalyzer::classify (s.name);   // instrument + group + sub-role + L/R side
        p.group      = c.matched ? c.group : juce::String ("Other");
        p.instrument = c.instrument;
        p.subRole    = c.subRole;
        p.side       = c.side;

        const auto lv = fileLevels (s.source);   // file I/O — caller runs us off the message thread
        p.rmsDb  = lv.rmsDb;
        p.peakDb = lv.peakDb;

        out.push_back (p);
    }

    // Exactly ONE lead vocal: prefer a "Lead" sub-role (named lead/main if several),
    // else the first vocal. Every other vocal stays a supporting layer (the MixingEngine
    // tucks backings/doubles/adlibs under it using subRole + side).
    int lead = -1, firstVocal = -1;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].group != "Vocals")
            continue;
        if (firstVocal < 0)
            firstVocal = (int) i;
        if (out[i].subRole == "Lead")
        {
            const auto n = out[i].name.toLowerCase();
            if (lead < 0 || n.contains ("lead") || n.contains ("main"))
                lead = (int) i;
        }
    }
    if (lead < 0)
        lead = firstVocal;
    if (lead >= 0)
        out[(size_t) lead].isLeadVocal = true;

    return out;
}

std::vector<TrackMixProfile> MixApplyEngine::readProfiles() const
{
    return profilesFromScan (scanTracks());
}

//==============================================================================
void MixApplyEngine::applyPlan (const MixPlan& plan)
{
    if (plan.isEmpty())
        return;

    const juce::String desc = plan.summary.isNotEmpty() ? plan.summary : juce::String ("KI-Mix");
    commandBridge.enqueue (desc, [this, plan] (juce::UndoManager& um)
    {
        applyMixNow (plan, um);
    });
}

//==============================================================================
void MixApplyEngine::applyMixNow (const MixPlan& plan, juce::UndoManager& um)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    const auto audioTracks = te::getAudioTracks (*edit);
    auto findTrack = [&] (tracktion::engine::EditItemID id) -> te::AudioTrack*
    {
        for (auto* t : audioTracks)
            if (t->itemID == id)
                return t;
        return nullptr;
    };

    // 1) Collect EQ moves per track → ONE Cloudes EQ per track (bands in plan order).
    //    EQ moves targeting the master (REF-03 style tilt) fold into ONE master EQ — same
    //    master-name convention the glue compressor uses.
    std::map<te::AudioTrack*, std::vector<EQBand>> eqByTrack;
    std::vector<EQBand> masterEqBands;
    for (const auto& a : plan.actions)
        if (isEqAction (a.type))
        {
            if (a.targetName == kMasterTarget)
                masterEqBands.push_back (toEqBand (a));
            else if (auto* t = findTrack (a.targetTrack))
                eqByTrack[t].push_back (toEqBand (a));
        }

    for (auto& [track, bands] : eqByTrack)
    {
        insertBuiltIn (*edit, track->pluginList, "Cloudes EQ",
                       [&bands] (juce::AudioPluginInstance& inst)
        {
            if (auto* eq = dynamic_cast<EQProcessor*> (&inst))
                for (int i = 0; i < (int) bands.size() && i < EQProcessor::NumBands; ++i)
                    eq->setBand (i, bands[(size_t) i]);
        });
    }

    if (! masterEqBands.empty())
    {
        insertBuiltIn (*edit, edit->getMasterPluginList(), "Cloudes EQ",
                       [&masterEqBands] (juce::AudioPluginInstance& inst)
        {
            if (auto* eq = dynamic_cast<EQProcessor*> (&inst))
                for (int i = 0; i < (int) masterEqBands.size() && i < EQProcessor::NumBands; ++i)
                    eq->setBand (i, masterEqBands[(size_t) i]);
        });
    }

    // 2) Everything else, in plan order.
    for (const auto& a : plan.actions)
    {
        if (isEqAction (a.type))
            continue;   // already folded into the per-track EQ

        // Master-bus glue: a Compress targeting "Master" → master plugin list.
        if (a.type == MixActionType::Compress && a.targetName == kMasterTarget)
        {
            insertBuiltIn (*edit, edit->getMasterPluginList(), "Cloudes Compressor",
                           [&a] (juce::AudioPluginInstance& inst)
            {
                setHostParam (inst, "ratio",     a.ratio);
                setHostParam (inst, "threshold", a.thresholdDb);
                setHostParam (inst, "makeup",    a.makeupGainDb);
            });
            continue;
        }

        auto* track = findTrack (a.targetTrack);
        if (track == nullptr)
            continue;

        switch (a.type)
        {
            case MixActionType::SetTrackGain:
                setTrackGainNoTxn (*track, a.gainDb, um);
                break;

            case MixActionType::SetTrackPan:
                setTrackPanNoTxn (*track, a.pan, um);
                break;

            case MixActionType::Compress:
                insertBuiltIn (*edit, track->pluginList, "Cloudes Compressor",
                               [&a] (juce::AudioPluginInstance& inst)
                {
                    setHostParam (inst, "ratio",     a.ratio);
                    setHostParam (inst, "threshold", a.thresholdDb);
                    setHostParam (inst, "makeup",    a.makeupGainDb);
                });
                break;

            case MixActionType::DeEss:
                insertBuiltIn (*edit, track->pluginList, "Cloudes De-Esser",
                               [&a] (juce::AudioPluginInstance& inst)
                {
                    // MixAction carries no Range — derive it from the de-ess ratio.
                    const float range = juce::jlimit (4.0f, 12.0f, a.ratio * 2.0f);
                    setHostParam (inst, "frequency", a.freqHz);
                    setHostParam (inst, "threshold", a.thresholdDb);
                    setHostParam (inst, "range",     range);
                });
                break;

            default:
                break;   // EQ types handled above
        }
    }

    // 3) Sub-bus routing (backings / adlibs) — pro vocal bussing. Create each named
    //    bus once, route its members in, nest it under the existing Vocals bus (else
    //    master), and glue it gently. All inside the same batch → one undo.
    auto findBusByName = [&] (const juce::String& name) -> te::AudioTrack*
    {
        for (auto* b : mixerEngine.getBusChannels())
            if (b->getName() == name)
                return b;
        return nullptr;
    };

    std::map<juce::String, std::vector<te::AudioTrack*>> byBus;
    for (const auto& r : plan.routings)
        if (auto* tr = findTrack (r.trackId))
            byBus[r.busName].push_back (tr);

    for (auto& [busName, trks] : byBus)
    {
        te::AudioTrack* bus = findBusByName (busName);
        const bool created = (bus == nullptr);
        if (created)
            bus = mixerEngine.createBusChannel (busName);
        if (bus == nullptr)
            continue;

        for (auto* tr : trks)
            mixerEngine.routeTrackToBus (*tr, *bus);

        if (created)
        {
            // Nest under the Vocals bus if the session has one (Backings → Vocals → Master).
            if (auto* vocalsBus = findBusByName ("Vocals"); vocalsBus != nullptr && vocalsBus != bus)
                mixerEngine.routeTrackToBus (*bus, *vocalsBus);

            // Gentle glue on the sub-bus — the layers move as one.
            insertBuiltIn (*edit, bus->pluginList, "Cloudes Compressor",
                           [] (juce::AudioPluginInstance& inst)
            {
                setHostParam (inst, "ratio",     2.0f);
                setHostParam (inst, "threshold", -14.0f);
                setHostParam (inst, "makeup",    1.0f);
            });
        }
    }
}

//==============================================================================
void MixApplyEngine::addReverbSend (te::EditItemID trackId, float amountNorm)
{
    commandBridge.enqueue ("KI-Studio: Reverb-Send", [this, trackId, amountNorm] (juce::UndoManager&)
    {
        auto* edit = editManager.getEdit();
        if (edit == nullptr)
            return;

        te::AudioTrack* track = nullptr;
        for (auto* t : te::getAudioTracks (*edit))
            if (t->itemID == trackId) { track = t; break; }
        if (track == nullptr)
            return;

        // Shared "Reverb" FX bus — created once with a 100%-wet Cloudes Reverb on the
        // return, reused on later sends (the way pros bus reverb instead of per-track inserts).
        te::AudioTrack* fx = nullptr;
        for (auto* f : mixerEngine.getFXChannels())
            if (f->getName() == "Reverb") { fx = f; break; }

        if (fx == nullptr)
        {
            fx = mixerEngine.createFXChannel ("Reverb");
            if (fx != nullptr)
                insertBuiltIn (*edit, fx->pluginList, "Cloudes Reverb",
                               [] (juce::AudioPluginInstance& inst) { setHostParam (inst, "wet", 1.0f); });
        }
        if (fx == nullptr)
            return;

        // amount 0..1 → send level in dB (1.0 = unity, 0.3 ≈ -10 dB).
        const float dB = juce::jlimit (-40.0f, 0.0f,
                                       juce::Decibels::gainToDecibels (juce::jlimit (0.001f, 1.0f, amountNorm)));
        mixerEngine.createFXSend (*track, *fx, dB);
    });
}
