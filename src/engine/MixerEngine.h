#pragma once

#include "EditManager.h"

/**
 * MixerEngine — Channel strip parameters, bus/FX routing, and sidechain routing
 * over Tracktion Engine.
 *
 * Covers requirements:
 *   MIX-01: Volume, pan, solo, mute on each AudioTrack
 *   MIX-02: Bus Channel and FX Channel creation, routing, and send management
 *   MIX-03: Sidechain routing — connect sourceTrack as sidechain for a plugin
 *
 * Channel type tagging:
 *   Bus and FX channel tracks carry the property kChannelTypeProp on their
 *   ValueTree state so MixerView and ArrangementView can distinguish them from
 *   normal audio tracks without scanning plugin chains.
 *
 *   kChannelTypeBus — Bus Channel: source tracks redirect their main output to
 *                     this track via TrackOutput::setOutputToTrack(). The bus
 *                     receives the summed signal, applies its insert chain and
 *                     fader, then outputs to master. Source track no longer feeds
 *                     master directly — true submix routing, not a copy.
 *
 *   kChannelTypeFX  — FX Channel: source tracks add an AuxSendPlugin (parallel
 *                     tap) targeting this track's AuxReturnPlugin. Source tracks
 *                     continue to master simultaneously (wet/dry send model).
 *                     FX Channel inserts (e.g. Reverb at 100% wet) process the
 *                     tapped signal before it reaches master.
 *
 * Design invariants:
 *   - All parameter mutations route through editManager.getUndoManager()
 *   - Each user action (setVolume, setPan, setSolo, setMute) begins a new
 *     transaction so it is individually undoable
 *   - AI batch mutations should call editManager.beginAITransaction() once
 *     before calling multiple MixerEngine methods
 */

/** ValueTree property name used to tag Bus and FX AudioTracks. */
static const juce::Identifier kChannelTypeProp { "aizanaChannelType" };
/** Property value for Bus Channel AudioTracks. */
static const juce::String kChannelTypeBus { "bus" };
/** Property value for FX Channel AudioTracks. */
static const juce::String kChannelTypeFX  { "fx" };
/** Property value for VCA Channel AudioTracks (group fader — no audio path). */
static const juce::String kChannelTypeVCA { "vca" };
/** Property value for Cue Channel AudioTracks (P6 — headphone/monitor mix routed
 *  to a selectable hardware output pair instead of master). */
static const juce::String kChannelTypeCue { "cue" };

/** Property stored on a member track: EditItemID string of its VCA channel. */
static const juce::Identifier kVCAAssignmentProp { "aizanaVCA" };

/** Custom property stored on a source track when it is routed to a Bus Channel.
 *  Caches the bus name so the channel strip button can display it reliably even
 *  when TE's getDestinationTrack() is transiently unavailable during graph updates.
 *  Cleared by unrouteFromBus(). */
static const juce::Identifier kOutputBusNameProp { "aizanaOutputBusName" };

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

    /** Return track pan from VolumeAndPanPlugin CachedValue (-1.0 to +1.0). */
    float getPan (const te::AudioTrack& track) const;

    /**
     * LIVE-Werte für die Automations-Anzeige (Fader folgt der Kurve beim
     * Playback, wie in Studio One/Logic).
     *
     * Bewusste I-02-AUSNAHME: getVolume()/getPan() lesen die CachedValues
     * (= vom User gesetzter Wert, korrekt nach Undo). Automation-Playback
     * schreibt aber NIE ins ValueTree — sie setzt nur den currentValue des
     * AutomatableParameter. Für die Anzeige WÄHREND des Playbacks ist deshalb
     * currentValue die einzige richtige Quelle. NIEMALS mit diesen Werten
     * zurückschreiben (kein setVolume/setPan aus einem Anzeige-Sync!).
     */
    float getLiveVolumeDb (const te::AudioTrack& track) const;
    float getLivePan      (const te::AudioTrack& track) const;

    /** Solo this track (mutes all other non-solo-isolated tracks). Undoable. */
    void setSolo (te::AudioTrack& track, bool solo);

    /** Mute/unmute this track. Undoable. */
    void setMute (te::AudioTrack& track, bool mute);

    // Selected-track commands: one transaction for the complete group.
    void toggleMuteGroup (const juce::Array<te::AudioTrack*>& tracks);
    void toggleSoloGroup (const juce::Array<te::AudioTrack*>& tracks);

    /** Returns true if the track is currently explicitly soloed. */
    bool isSoloed (const te::AudioTrack& track) const;

    /** Returns true if the track is currently explicitly muted. */
    bool isMuted (const te::AudioTrack& track) const;

    // -------------------------------------------------------------------------
    // MIX-02: Bus Channel and FX Channel creation + routing
    // -------------------------------------------------------------------------

    /**
     * Create a Bus Channel — an AudioTrack tagged kChannelTypeBus.
     *
     * Signal path: source tracks redirect their main output via
     * TrackOutput::setOutputToTrack() → Bus inserts → Bus fader → master.
     * The source track's direct-to-master path is replaced (not parallel).
     *
     * Returns the new AudioTrack, or nullptr on failure.
     */
    te::AudioTrack* createBusChannel (const juce::String& name);

    /**
     * Create an FX Channel — an AudioTrack with AuxReturnPlugin, tagged kChannelTypeFX.
     *
     * Signal path: source tracks add AuxSendPlugin (parallel tap) →
     * FX Channel AuxReturnPlugin → inserts (e.g. Reverb 100% wet) → FX fader → master.
     * Source track main output continues to master simultaneously.
     *
     * Returns the new AudioTrack, or nullptr on failure.
     */
    te::AudioTrack* createFXChannel (const juce::String& name);

    /**
     * Redirect sourceTrack's main output into busChannel.
     * Uses TrackOutput::setOutputToTrack() — source track no longer feeds master directly.
     * No-op if busChannel is not tagged as kChannelTypeBus.
     */
    void routeTrackToBus (te::AudioTrack& sourceTrack, te::AudioTrack& busChannel);

    /**
     * Restore sourceTrack's main output to the default audio output (master).
     * Call this when removing a track from a Bus Channel.
     */
    void unrouteFromBus (te::AudioTrack& sourceTrack);

    /**
     * Returns the Bus Channel that sourceTrack currently routes its main output to,
     * or nullptr if the track goes directly to master (or to a non-bus track).
     */
    te::AudioTrack* getBusChannelFor (const te::AudioTrack& sourceTrack) const;

    /**
     * Insert an AuxSendPlugin on sourceTrack targeting fxChannel at sendLeveldB.
     * Returns the new AuxSendPlugin, or nullptr on failure.
     * No-op (returns nullptr) if fxChannel is not tagged as kChannelTypeFX.
     */
    te::AuxSendPlugin* createFXSend (te::AudioTrack& sourceTrack,
                                     te::AudioTrack& fxChannel,
                                     float sendLeveldB = -6.0f);

    /**
     * Remove the AuxSendPlugin on sourceTrack that targets fxChannel.
     * No-op if no matching send is found.
     */
    void removeFXSend (te::AudioTrack& sourceTrack, te::AudioTrack& fxChannel);

    /**
     * Set the send level (dB) for the AuxSendPlugin on sourceTrack targeting fxChannel.
     * No-op if no send exists.
     */
    void setFXSendLevel (te::AudioTrack& sourceTrack, te::AudioTrack& fxChannel, float dB);

    /** Returns the current send level in dB, or -100.0f if no send exists. */
    float getFXSendLevel (const te::AudioTrack& sourceTrack, const te::AudioTrack& fxChannel) const;

    /**
     * Enable or disable the AuxSendPlugin on sourceTrack targeting fxChannel.
     */
    void setFXSendEnabled (te::AudioTrack& sourceTrack, te::AudioTrack& fxChannel, bool enabled);

    /** Returns true if the send from sourceTrack to fxChannel exists and is enabled. */
    bool isFXSendEnabled (const te::AudioTrack& sourceTrack, const te::AudioTrack& fxChannel) const;

    /**
     * Returns true if the send from sourceTrack to fxChannel is pre-fader
     * (AuxSendPlugin positioned before the VolumeAndPanPlugin in the chain).
     * Returns false if post-fader (after VolumeAndPanPlugin) or if no send exists.
     */
    bool isFXSendPreFader (const te::AudioTrack& sourceTrack, const te::AudioTrack& fxChannel) const;

    /**
     * Move the AuxSendPlugin on sourceTrack for fxChannel to a pre-fader or
     * post-fader position relative to the track's VolumeAndPanPlugin.
     * No-op if no send exists or VolumeAndPanPlugin is not found.
     */
    void setFXSendPreFader (te::AudioTrack& sourceTrack, te::AudioTrack& fxChannel, bool preFader);

    /** Returns all Bus Channel AudioTracks in the current Edit. */
    juce::Array<te::AudioTrack*> getBusChannels() const;

    /** Returns all FX Channel AudioTracks in the current Edit. */
    juce::Array<te::AudioTrack*> getFXChannels() const;

    /** Returns true if track is tagged as a Bus Channel. */
    static bool isBusChannel (const te::AudioTrack& track);

    /** Returns true if track is tagged as an FX Channel. */
    static bool isFXChannel (const te::AudioTrack& track);

    /** Returns true if track is a plain audio track (no channel-type tag). */
    static bool isAudioChannel (const te::AudioTrack& track);

    // -------------------------------------------------------------------------
    // P6: Cue Channels (Kopfhörer-Mixes fürs Tracking — Studio One "Cue Mix")
    // -------------------------------------------------------------------------

    /**
     * Create a Cue Channel — an AudioTrack with AuxReturnPlugin, tagged
     * kChannelTypeCue. Same send/return plumbing as an FX Channel, but its
     * output is meant to feed a hardware output pair (headphone amp) instead
     * of the master bus. Freshly created cue channels output to the default
     * device until setCueOutputDeviceID() picks a dedicated pair.
     *
     * Sends to a cue channel default to PRE-FADER (pro standard: the artist's
     * headphone mix must not follow main-mix fader moves).
     */
    te::AudioTrack* createCueChannel (const juce::String& name);

    /** Returns all Cue Channel AudioTracks in the current Edit. */
    juce::Array<te::AudioTrack*> getCueChannels() const;

    /** Returns true if track is tagged as a Cue Channel. */
    static bool isCueChannel (const te::AudioTrack& track);

    /**
     * Route the cue channel's output to the given hardware output device
     * (OutputDevice::getDeviceID()). Empty deviceID = back to the default
     * audio output. Undoable (one transaction).
     */
    void setCueOutputDeviceID (te::AudioTrack& cueChannel, const juce::String& deviceID);

    /** Display name of the cue channel's current output destination. */
    juce::String getCueOutputName (const te::AudioTrack& cueChannel) const;

    // -------------------------------------------------------------------------
    // VCA channels (group faders — Studio One pattern, Aizana semantics:
    // the VCA fader moves its members' faders RELATIVELY; no audio routing).
    // -------------------------------------------------------------------------

    /** Create a VCA Channel — an AudioTrack tagged kChannelTypeVCA. It carries
        no clips and no routing; its VolumeAndPanPlugin stores the fader pos. */
    te::AudioTrack* createVCAChannel (const juce::String& name);

    /** Assign a track to a VCA (stores the VCA's EditItemID on the member). Undoable. */
    void assignTrackToVCA (te::AudioTrack& track, te::AudioTrack& vca);

    /** Remove a track's VCA assignment. Undoable. */
    void unassignFromVCA (te::AudioTrack& track);

    /** The VCA channel a track is assigned to, or nullptr. */
    te::AudioTrack* getVCAFor (const te::AudioTrack& track) const;

    /** All VCA channels in the edit. */
    juce::Array<te::AudioTrack*> getVCAChannels() const;

    /** All member tracks assigned to the given VCA. */
    juce::Array<te::AudioTrack*> getVCAMembers (const te::AudioTrack& vca) const;

    /** Returns true if track is tagged as a VCA Channel. */
    static bool isVCAChannel (const te::AudioTrack& track);

    // ---- Legacy API (for existing callers) ----
    te::AudioTrack*    createBus  (const juce::String& name);
    te::AuxSendPlugin* createSend (te::AudioTrack& sourceTrack,
                                   te::AudioTrack& returnTrack,
                                   float sendLeveldB);
    void               removeSend (te::AudioTrack& sourceTrack, te::AudioTrack& returnTrack);

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

    /** Return the bus number stored in the AuxReturnPlugin on returnTrack, or -1 if none. */
    int getBusNumberForReturnTrack (const te::AudioTrack& returnTrack) const;

    /** Shared helper for createFXSend and legacy createSend. */
    te::AuxSendPlugin* createAuxSend (te::AudioTrack& sourceTrack,
                                      te::AudioTrack& returnTrack,
                                      float sendLeveldB);

    /** Shared body of createFXChannel/createCueChannel — AuxReturn channel with
        the given channel-type tag. */
    te::AudioTrack* createReturnChannelInternal (const juce::String& name,
                                                 const juce::String& channelTypeTag);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerEngine)
};
