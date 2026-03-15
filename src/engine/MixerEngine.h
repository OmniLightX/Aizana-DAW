#pragma once

#include "EditManager.h"

/**
 * MixerEngine — Channel strip parameters, bus/send routing, and sidechain routing
 * over Tracktion Engine.
 *
 * Covers requirements:
 *   MIX-01: Volume, pan, solo, mute on each AudioTrack
 *   MIX-02: Bus (FolderTrack) and send (AuxSendPlugin) routing for submixing
 *   MIX-03: Sidechain routing — connect sourceTrack as sidechain for a plugin
 *
 * Design invariants:
 *   - All parameter mutations route through editManager.getUndoManager()
 *   - Each user action (setVolume, setPan, setSolo, setMute) begins a new
 *     transaction so it is individually undoable
 *   - AI batch mutations should call editManager.beginAITransaction() once
 *     before calling multiple MixerEngine methods
 */
class MixerEngine
{
public:
    explicit MixerEngine (EditManager& editManager);

    // -------------------------------------------------------------------------
    // MIX-01: Channel strip parameters
    // -------------------------------------------------------------------------

    /** Set track volume in dB (-144 = -inf, +6 = max). Undoable. */
    void setVolume (te::AudioTrack& track, float dB);

    /** Return track volume in dB from VolumeAndPanPlugin. */
    float getVolume (const te::AudioTrack& track) const;

    /** Set track pan (-1.0 = L, 0.0 = C, 1.0 = R). Undoable. */
    void setPan (te::AudioTrack& track, float pan);

    /** Solo this track (mutes all other non-solo-isolated tracks). Undoable. */
    void setSolo (te::AudioTrack& track, bool solo);

    /** Mute/unmute this track. Undoable. */
    void setMute (te::AudioTrack& track, bool mute);

    /** Returns true if the track is currently explicitly soloed. */
    bool isSoloed (const te::AudioTrack& track) const;

    /** Returns true if the track is currently explicitly muted. */
    bool isMuted (const te::AudioTrack& track) const;

    // -------------------------------------------------------------------------
    // MIX-02: Bus and send routing
    // -------------------------------------------------------------------------

    /**
     * Create a submix bus (FolderTrack). Returns the new FolderTrack.
     * The FolderTrack acts as a submix bus — insert tracks under it or route
     * sends to it via createSend().
     */
    te::FolderTrack* createBus (const juce::String& name);

    /**
     * Insert an AuxSendPlugin on sourceTrack targeting destBus, at sendLeveldB.
     * Returns the new AuxSendPlugin, or nullptr on failure.
     * The bus number is derived from destBus's position in the edit track list.
     */
    te::AuxSendPlugin* createSend (te::AudioTrack& sourceTrack,
                                   te::FolderTrack& destBus,
                                   float sendLeveldB);

    /**
     * Remove the AuxSendPlugin(s) on sourceTrack that target destBus.
     * No-op if no matching send is found.
     */
    void removeSend (te::AudioTrack& sourceTrack, te::FolderTrack& destBus);

    // -------------------------------------------------------------------------
    // MIX-03: Sidechain routing
    // -------------------------------------------------------------------------

    /**
     * Mark sourceTrack as the sidechain source for the given plugin.
     *
     * In Tracktion Engine, sidechain routing is declared via the plugin's
     * sidechain state in the Edit ValueTree. This method sets the
     * sidechainSourceID on the plugin's ValueTree state to the source track's
     * EditItemID, and marks sourceTrack as a sidechain source so the engine
     * keeps routing audio from it even when the track is "muted by solo".
     */
    void setSidechainSource (te::Plugin& plugin, te::AudioTrack& sourceTrack);

    /** Remove sidechain source from the plugin. */
    void clearSidechainSource (te::Plugin& plugin);

private:
    EditManager& editManager;

    /** Return the bus number for a FolderTrack (its 0-based index in edit track list). */
    int getBusNumberForFolderTrack (const te::FolderTrack& bus) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerEngine)
};
