#include "MixerEngine.h"

//==============================================================================
MixerEngine::MixerEngine (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
// MIX-01: Channel strip — volume, pan, solo, mute
//==============================================================================

void MixerEngine::setVolume (te::AudioTrack& track, float dB)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* vp = track.getVolumePlugin();
    if (vp == nullptr)
        return;

    const float faderPos = te::decibelsToVolumeFaderPosition (dB);
    edit->getUndoManager().beginNewTransaction ("Set volume");
    vp->volume.setValue (faderPos, &edit->getUndoManager());
    vp->setSliderPos (faderPos);
}

float MixerEngine::getVolume (const te::AudioTrack& track) const
{
    if (auto* vp = const_cast<te::AudioTrack&> (track).getVolumePlugin())
        return te::volumeFaderPositionToDB (vp->volume.get());

    return 0.0f;
}

void MixerEngine::setPan (te::AudioTrack& track, float pan)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* vp = track.getVolumePlugin();
    if (vp == nullptr)
        return;

    const float clamped = juce::jlimit (-1.0f, 1.0f, pan);
    edit->getUndoManager().beginNewTransaction ("Set pan");
    vp->pan.setValue (clamped, &edit->getUndoManager());
    vp->setPan (clamped);
}

float MixerEngine::getPan (const te::AudioTrack& track) const
{
    if (auto* vp = const_cast<te::AudioTrack&> (track).getVolumePlugin())
        return vp->pan.get();

    return 0.0f;
}

float MixerEngine::getLiveVolumeDb (const te::AudioTrack& track) const
{
    // currentValue-basiert (s. Header): folgt der Automations-Kurve beim Playback
    if (auto* vp = const_cast<te::AudioTrack&> (track).getVolumePlugin())
        return vp->getVolumeDb();

    return 0.0f;
}

float MixerEngine::getLivePan (const te::AudioTrack& track) const
{
    if (auto* vp = const_cast<te::AudioTrack&> (track).getVolumePlugin())
        return vp->getPan();

    return 0.0f;
}

void MixerEngine::setSolo (te::AudioTrack& track, bool solo)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    edit->getUndoManager().beginNewTransaction (solo ? "Solo track" : "Unsolo track");
    // setSoloIsolate(false) MUST be called before setSolo, or TE silently ignores the call.
    track.setSoloIsolate (false);
    track.setSolo (solo);
}

void MixerEngine::setMute (te::AudioTrack& track, bool mute)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    edit->getUndoManager().beginNewTransaction (mute ? "Mute track" : "Unmute track");
    track.setMute (mute);
}

bool MixerEngine::isSoloed (const te::AudioTrack& track) const
{
    return track.isSolo (false);
}

bool MixerEngine::isMuted (const te::AudioTrack& track) const
{
    return track.isMuted (false);
}

//==============================================================================
// MIX-02: Bus Channel and FX Channel
//==============================================================================

// ---- Static classification helpers ----

bool MixerEngine::isBusChannel (const te::AudioTrack& track)
{
    return track.state.getProperty (kChannelTypeProp).toString() == kChannelTypeBus;
}

bool MixerEngine::isFXChannel (const te::AudioTrack& track)
{
    return track.state.getProperty (kChannelTypeProp).toString() == kChannelTypeFX;
}

bool MixerEngine::isAudioChannel (const te::AudioTrack& track)
{
    const juce::String tag = track.state.getProperty (kChannelTypeProp).toString();
    return tag.isEmpty();   // no tag → plain audio track
}

bool MixerEngine::isVCAChannel (const te::AudioTrack& track)
{
    return track.state.getProperty (kChannelTypeProp).toString() == kChannelTypeVCA;
}

// ---- VCA channels ----

te::AudioTrack* MixerEngine::createVCAChannel (const juce::String& name)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Append at END — same invariant as Bus/FX channels (no renumbering).
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (track == nullptr)
        return nullptr;

    track->setName (name);
    track->state.setProperty (kChannelTypeProp, kChannelTypeVCA, nullptr);
    track->setSoloIsolate (false);

    // The VCA carries no audio; its VolumeAndPanPlugin only stores the fader
    // position (and could later host VCA automation).
    if (track->getVolumePlugin() == nullptr)
    {
        auto vp = edit->getPluginCache().createNewPlugin (
            te::VolumeAndPanPlugin::xmlTypeName, {});
        track->pluginList.insertPlugin (vp, 0, nullptr);
    }

    return track.get();
}

void MixerEngine::assignTrackToVCA (te::AudioTrack& track, te::AudioTrack& vca)
{
    if (! isVCAChannel (vca))
        return;
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Assign VCA");
    track.state.setProperty (kVCAAssignmentProp, vca.itemID.toString(), &um);
}

void MixerEngine::unassignFromVCA (te::AudioTrack& track)
{
    if (! track.state.hasProperty (kVCAAssignmentProp))
        return;
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Unassign VCA");
    track.state.removeProperty (kVCAAssignmentProp, &um);
}

te::AudioTrack* MixerEngine::getVCAFor (const te::AudioTrack& track) const
{
    const auto id = track.state.getProperty (kVCAAssignmentProp).toString();
    if (id.isEmpty())
        return nullptr;
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;
    for (auto* t : te::getAudioTracks (*edit))
        if (isVCAChannel (*t) && t->itemID.toString() == id)
            return t;
    return nullptr;
}

juce::Array<te::AudioTrack*> MixerEngine::getVCAChannels() const
{
    juce::Array<te::AudioTrack*> out;
    if (auto* edit = editManager.getEdit())
        for (auto* t : te::getAudioTracks (*edit))
            if (isVCAChannel (*t))
                out.add (t);
    return out;
}

juce::Array<te::AudioTrack*> MixerEngine::getVCAMembers (const te::AudioTrack& vca) const
{
    juce::Array<te::AudioTrack*> out;
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return out;
    const auto id = vca.itemID.toString();
    for (auto* t : te::getAudioTracks (*edit))
        if (t->state.getProperty (kVCAAssignmentProp).toString() == id)
            out.add (t);
    return out;
}

// ---- Creation ----

te::AudioTrack* MixerEngine::createBusChannel (const juce::String& name)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Append after all existing tracks.
    // TrackInsertPoint { nullptr, nullptr } means INSERT AT START (preceding=nullptr),
    // which would shift Track 1 → Track 2 and break all track-number-based routing.
    // getEndOfTracks() is the correct way to append without renumbering.
    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (track == nullptr)
        return nullptr;

    track->setName (name);

    // Tag as Bus Channel — ArrangementView and MixerView use this to classify strips
    track->state.setProperty (kChannelTypeProp, kChannelTypeBus, nullptr);

    // Ensure Bus Channel output goes to master (default device).
    // Explicit call guards against edge cases where TE's default initialisation
    // might not fire (e.g. the edit already has active playback).
    track->getOutput().setOutputToDefaultDevice (false);

    // New Bus tracks must not be solo-isolated — solo on other tracks must mute them.
    track->setSoloIsolate (false);

    // Guard: TE normally auto-creates VolumeAndPanPlugin for every AudioTrack, but
    // ensure it exists so setVolume() / getVolume() don't silently no-op on this track.
    if (track->getVolumePlugin() == nullptr)
    {
        auto vp = edit->getPluginCache().createNewPlugin (
            te::VolumeAndPanPlugin::xmlTypeName, {});
        track->pluginList.insertPlugin (vp, 0, nullptr);
    }

    return track.get();
}

te::AudioTrack* MixerEngine::createFXChannel (const juce::String& name)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Find next unused bus number for the AuxReturn pair
    int nextBusNum = 0;
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* p : t->pluginList.getPlugins())
            if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (p))
                nextBusNum = juce::jmax (nextBusNum, ret->busNumber.get() + 1);

    auto track = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr);
    if (track == nullptr)
        return nullptr;

    track->setName (name);

    // Tag as FX Channel — hides it from ArrangementView, identifies it in Mixer
    track->state.setProperty (kChannelTypeProp, kChannelTypeFX, nullptr);

    // Also store bus number directly on the track state so callers can read it
    // without scanning the plugin chain (fallback if AuxReturnPlugin lookup fails).
    track->state.setProperty ("aizanaBusNumber", nextBusNum, nullptr);

    // New tracks default to solo-isolated off so solo works correctly on other tracks.
    track->setSoloIsolate (false);

    // Guard: ensure VolumeAndPanPlugin exists for fader control on this FX channel.
    // TE normally auto-creates it, but guard against edge cases (same pattern as createBusChannel).
    if (track->getVolumePlugin() == nullptr)
    {
        auto vp = edit->getPluginCache().createNewPlugin (
            te::VolumeAndPanPlugin::xmlTypeName, {});
        track->pluginList.insertPlugin (vp, 1, nullptr);  // index 1: after AuxReturnPlugin at 0
    }

    // Insert AuxReturnPlugin at position 0 so it receives sends before user inserts.
    // Signal flow: AuxReturn (receives bus) → user inserts (Reverb etc.) → VolumeAndPan → master.
    auto returnPlugin = edit->getPluginCache().createNewPlugin (
        te::AuxReturnPlugin::xmlTypeName, {});
    if (returnPlugin != nullptr)
    {
        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (returnPlugin.get()))
            ret->busNumber.setValue (nextBusNum, nullptr);
        track->pluginList.insertPlugin (returnPlugin, 0, nullptr);
    }

    // Register the FX channel name in TE's aux bus name registry
    edit->setAuxBusName (nextBusNum, name);

    // Ensure FX Channel output goes to master — AuxReturn receives audio from
    // the aux bus, processes through inserts, then fader outputs to master.
    track->getOutput().setOutputToDefaultDevice (false);

    return track.get();
}

// ---- Bus routing (main-output redirect) ----

void MixerEngine::routeTrackToBus (te::AudioTrack& sourceTrack, te::AudioTrack& busChannel)
{
    if (! isBusChannel (busChannel))
        return;

    // Redirect sourceTrack's main output to the Bus AudioTrack.
    // TE's render graph routes the source through the bus track's plugin chain
    // (inserts + fader), which then outputs to master via busChannel's own output.
    // The bus track output must remain on the default audio device (master) — it is
    // set to that in createBusChannel() and must never be redirected to another track.
    sourceTrack.getOutput().setOutputToTrack (&busChannel);

    // Cache the bus name so the channel strip button can display it reliably.
    // TE's getDestinationTrack() can transiently return nullptr during graph updates;
    // kOutputBusNameProp is our authoritative display cache.
    sourceTrack.state.setProperty (kOutputBusNameProp, busChannel.getName(), nullptr);
}

void MixerEngine::unrouteFromBus (te::AudioTrack& sourceTrack)
{
    // Restore the track's output to the default audio output device (master)
    sourceTrack.getOutput().setOutputToDefaultDevice (false);
    // Clear the cached display property so the button reverts to "▶ Master"
    sourceTrack.state.removeProperty (kOutputBusNameProp, nullptr);
}

te::AudioTrack* MixerEngine::getBusChannelFor (const te::AudioTrack& sourceTrack) const
{
    auto* dest = const_cast<te::AudioTrack&> (sourceTrack).getOutput().getDestinationTrack();
    if (dest == nullptr)
        return nullptr;

    return isBusChannel (*dest) ? dest : nullptr;
}

// ---- FX sends (parallel AuxSend/Return) ----

te::AuxSendPlugin* MixerEngine::createAuxSend (te::AudioTrack& sourceTrack,
                                                te::AudioTrack& returnTrack,
                                                float sendLeveldB)
{
    // Bus→bus sends are allowed (Studio One pattern: vocal bus → reverb bus,
    // delay bus → reverb bus), but a track must never send to ITSELF — that
    // would be a direct feedback loop on the FX return.
    if (&sourceTrack == &returnTrack)
        return nullptr;

    const int busNumber = getBusNumberForReturnTrack (returnTrack);
    if (busNumber < 0)
        return nullptr;

    auto pluginState = te::AuxSendPlugin::create();
    pluginState.setProperty (te::IDs::busNum, busNumber, nullptr);

    auto pluginPtr = sourceTrack.pluginList.insertPlugin (pluginState, -1);  // -1 = append
    auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*> (pluginPtr.get());
    if (sendPlugin != nullptr)
        sendPlugin->setGainDb (sendLeveldB);

    return sendPlugin;
}

te::AuxSendPlugin* MixerEngine::createFXSend (te::AudioTrack& sourceTrack,
                                               te::AudioTrack& fxChannel,
                                               float sendLeveldB)
{
    if (! isFXChannel (fxChannel))
        return nullptr;

    return createAuxSend (sourceTrack, fxChannel, sendLeveldB);
}

void MixerEngine::removeFXSend (te::AudioTrack& sourceTrack, te::AudioTrack& fxChannel)
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return;

    for (auto* p : sourceTrack.pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
            {
                send->removeFromParent();
                return;
            }
        }
    }
}

void MixerEngine::setFXSendLevel (te::AudioTrack& sourceTrack,
                                   te::AudioTrack& fxChannel,
                                   float dB)
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return;

    for (auto* p : sourceTrack.pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
            {
                send->setGainDb (dB);
                return;
            }
        }
    }
}

float MixerEngine::getFXSendLevel (const te::AudioTrack& sourceTrack,
                                    const te::AudioTrack& fxChannel) const
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return -100.0f;

    for (auto* p : const_cast<te::AudioTrack&> (sourceTrack).pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
            if (send->getBusNumber() == busNumber)
                return send->getGainDb();
    }

    return -100.0f;
}

void MixerEngine::setFXSendEnabled (te::AudioTrack& sourceTrack,
                                     te::AudioTrack& fxChannel,
                                     bool enabled)
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return;

    for (auto* p : sourceTrack.pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
            {
                send->setEnabled (enabled);
                return;
            }
        }
    }
}

bool MixerEngine::isFXSendEnabled (const te::AudioTrack& sourceTrack,
                                    const te::AudioTrack& fxChannel) const
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return false;

    for (auto* p : const_cast<te::AudioTrack&> (sourceTrack).pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
            if (send->getBusNumber() == busNumber)
                return send->isEnabled();
    }

    return false;
}

juce::Array<te::AudioTrack*> MixerEngine::getBusChannels() const
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return {};

    juce::Array<te::AudioTrack*> result;
    for (auto* t : te::getAudioTracks (*edit))
        if (isBusChannel (*t))
            result.add (t);
    return result;
}

juce::Array<te::AudioTrack*> MixerEngine::getFXChannels() const
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return {};

    juce::Array<te::AudioTrack*> result;
    for (auto* t : te::getAudioTracks (*edit))
        if (isFXChannel (*t))
            result.add (t);
    return result;
}

// ---- Legacy API (retained for existing callers) ----

te::AudioTrack* MixerEngine::createBus (const juce::String& name)
{
    return createBusChannel (name);
}

te::AuxSendPlugin* MixerEngine::createSend (te::AudioTrack& sourceTrack,
                                             te::AudioTrack& returnTrack,
                                             float sendLeveldB)
{
    return createAuxSend (sourceTrack, returnTrack, sendLeveldB);
}

void MixerEngine::removeSend (te::AudioTrack& sourceTrack, te::AudioTrack& returnTrack)
{
    const int busNumber = getBusNumberForReturnTrack (returnTrack);
    if (busNumber < 0)
        return;

    for (auto* p : sourceTrack.pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
            {
                editManager.getEdit()->getUndoManager().beginNewTransaction ("Remove send");
                send->removeFromParent();
                return;
            }
        }
    }
}

bool MixerEngine::isFXSendPreFader (const te::AudioTrack& sourceTrack,
                                     const te::AudioTrack& fxChannel) const
{
    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return false;

    auto& src = const_cast<te::AudioTrack&> (sourceTrack);
    auto* vp = src.getVolumePlugin();
    if (vp == nullptr)
        return false;

    auto& listState = src.pluginList.state;
    const int vpIdx = listState.indexOf (vp->state);

    for (auto* p : src.pluginList.getPlugins())
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
                return listState.indexOf (send->state) < vpIdx;
        }
    }
    return false;
}

void MixerEngine::setFXSendPreFader (te::AudioTrack& sourceTrack,
                                      te::AudioTrack& fxChannel,
                                      bool preFader)
{
    if (isFXSendPreFader (sourceTrack, fxChannel) == preFader)
        return;

    const int busNumber = getBusNumberForReturnTrack (fxChannel);
    if (busNumber < 0)
        return;

    te::AuxSendPlugin* sendPlugin = nullptr;
    for (auto* p : sourceTrack.pluginList.getPlugins())
    {
        if (auto* s = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (s->getBusNumber() == busNumber)
            {
                sendPlugin = s;
                break;
            }
        }
    }
    if (sendPlugin == nullptr)
        return;

    auto* vp = sourceTrack.getVolumePlugin();
    if (vp == nullptr)
        return;

    auto& listState    = sourceTrack.pluginList.state;
    const int sendIdx  = listState.indexOf (sendPlugin->state);
    const int vpIdx    = listState.indexOf (vp->state);

    if (preFader)
        listState.moveChild (sendIdx, vpIdx, nullptr);
    else
        listState.moveChild (sendIdx, listState.getNumChildren() - 1, nullptr);
}

int MixerEngine::getBusNumberForReturnTrack (const te::AudioTrack& returnTrack) const
{
    for (auto* p : const_cast<te::AudioTrack&> (returnTrack).pluginList.getPlugins())
        if (auto* ret = dynamic_cast<te::AuxReturnPlugin*> (p))
            return ret->busNumber.get();

    return -1;
}

//==============================================================================
// MIX-03: Sidechain routing
//==============================================================================

void MixerEngine::setSidechainSource (te::Plugin& plugin, te::AudioTrack& sourceTrack)
{
    plugin.setSidechainSourceID (sourceTrack.itemID);
}

void MixerEngine::clearSidechainSource (te::Plugin& plugin)
{
    plugin.setSidechainSourceID (te::EditItemID {});
}
