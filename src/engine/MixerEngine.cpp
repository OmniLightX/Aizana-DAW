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

    // setVolumeDb updates volParam->currentValue (read by getVolumeDb).
    // We also write the ValueTree property with the UndoManager so the change
    // is individually undoable via undoLastAction().
    edit->getUndoManager().beginNewTransaction ("Set volume");
    vp->setVolumeDb (dB);
    vp->volume.setValue (te::decibelsToVolumeFaderPosition (dB), &edit->getUndoManager());
}

float MixerEngine::getVolume (const te::AudioTrack& track) const
{
    if (auto* vp = const_cast<te::AudioTrack&> (track).getVolumePlugin())
        return vp->getVolumeDb();

    return 0.0f;
}

void MixerEngine::setPan (te::AudioTrack& track, float pan)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* vp = track.getVolumePlugin();
    if (vp == nullptr)
        return;

    // Use CachedValue with UndoManager for proper undo support.
    // Pan value is in [-1.0, 1.0] which matches the pan CachedValue range.
    edit->getUndoManager().beginNewTransaction ("Set pan");
    vp->setPan (juce::jlimit (-1.0f, 1.0f, pan));
    vp->pan.setValue (juce::jlimit (-1.0f, 1.0f, pan), &edit->getUndoManager());
}

void MixerEngine::setSolo (te::AudioTrack& track, bool solo)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Set via ValueTree state with UndoManager so the change is undoable.
    // Track::setSolo() sets the muted CachedValue without an UndoManager (no undo).
    // We go directly to the state so undo is tracked.
    edit->getUndoManager().beginNewTransaction (solo ? "Solo track" : "Unsolo track");
    track.state.setProperty (te::IDs::solo, solo, &edit->getUndoManager());
}

void MixerEngine::setMute (te::AudioTrack& track, bool mute)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Set via ValueTree state with UndoManager so the change is undoable.
    // Track::setMute() sets the muted CachedValue without an UndoManager (no undo).
    edit->getUndoManager().beginNewTransaction (mute ? "Mute track" : "Unmute track");
    track.state.setProperty (te::IDs::mute, mute, &edit->getUndoManager());
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
// MIX-02: Bus and send routing
//==============================================================================

te::FolderTrack* MixerEngine::createBus (const juce::String& name)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // insertNewFolderTrack(insertPoint, selectionManager, asSubmix)
    // asSubmix=true creates a FolderTrack that acts as a submix bus.
    // Insert at the end of the track list (after the last existing track).
    auto bus = edit->insertNewFolderTrack (te::TrackInsertPoint { nullptr, nullptr }, nullptr, true);

    if (bus != nullptr)
        bus->setName (name);

    return bus;
}

te::AuxSendPlugin* MixerEngine::createSend (te::AudioTrack& sourceTrack,
                                             te::FolderTrack& destBus,
                                             float sendLeveldB)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    // Insert an AuxSendPlugin into the source track's plugin chain.
    // The bus number is the 0-based index of the FolderTrack in the edit.
    int busNumber = getBusNumberForFolderTrack (destBus);

    // Build the ValueTree for an AuxSendPlugin with the target bus number
    auto pluginState = te::AuxSendPlugin::create();
    pluginState.setProperty ("busNum", busNumber, nullptr);

    auto pluginPtr = sourceTrack.pluginList.insertPlugin (pluginState, -1);  // -1 = append

    auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*> (pluginPtr.get());
    if (sendPlugin != nullptr)
        sendPlugin->setGainDb (sendLeveldB);

    return sendPlugin;
}

void MixerEngine::removeSend (te::AudioTrack& sourceTrack, te::FolderTrack& destBus)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    int busNumber = getBusNumberForFolderTrack (destBus);

    auto plugins = sourceTrack.pluginList.getPlugins();
    for (auto* p : plugins)
    {
        if (auto* send = dynamic_cast<te::AuxSendPlugin*> (p))
        {
            if (send->getBusNumber() == busNumber)
            {
                edit->getUndoManager().beginNewTransaction ("Remove send");
                send->removeFromParent();
                return;   // remove first matching send only
            }
        }
    }
}

int MixerEngine::getBusNumberForFolderTrack (const te::FolderTrack& bus) const
{
    // Tracktion Engine bus numbers are 0-based indices of FolderTracks in the edit.
    // We walk all tracks and count folder tracks to find this one's index.
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return 0;

    int busIdx = 0;
    for (auto* track : te::getAllTracks (*edit))
    {
        if (auto* folder = dynamic_cast<te::FolderTrack*> (track))
        {
            if (folder == &bus)
                return busIdx;
            ++busIdx;
        }
    }

    return 0;
}

//==============================================================================
// MIX-03: Sidechain routing
//==============================================================================

void MixerEngine::setSidechainSource (te::Plugin& plugin, te::AudioTrack& sourceTrack)
{
    // Tracktion Engine's Plugin has setSidechainSourceID(EditItemID) which sets the
    // ValueTree property "sidechainSourceID". The playback graph builder reads this
    // to route audio from sourceTrack into the plugin's sidechain (auxiliary) bus.
    plugin.setSidechainSourceID (sourceTrack.itemID);
}

void MixerEngine::clearSidechainSource (te::Plugin& plugin)
{
    plugin.setSidechainSourceID (te::EditItemID {});
}
