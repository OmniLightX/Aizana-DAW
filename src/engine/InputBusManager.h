#pragma once

#include "EditManager.h"

/**
 * InputBusManager — Named Software Input Buses (Phase 2, first task).
 *
 * Studio One concept: Hardware → Named Software Bus ("Vocal Mic", "Guitar In")
 * → Track input. Tracks never reference raw hardware channels; they reference a
 * named bus. This gives every input a stable, semantic name that survives device
 * reconfiguration — the prerequisite for Phase 3 AI instrument-detection /
 * auto-routing, which needs stable input names to reason about.
 *
 * ── Hybrid architecture (chosen 2026-05-31) ───────────────────────────────────
 *
 *   Edit ValueTree <INPUTBUSES>      ← SOURCE OF TRUTH (portable in the .session)
 *        │  on load / on change: applyToEngine()
 *        ▼
 *   te::WaveInputDevice::setAlias()  ← BINDING (Tracktion Engine stays the single
 *        │                             hardware-binding mechanism — no parallel
 *        ▼                             routing engine that could desync)
 *   Track::setTarget()               ← unchanged Phase 1 recording flow
 *
 * The bus definition (name + role + hardware channel mapping) lives in the Edit
 * ValueTree so it is saved with the session and is directly queryable by the
 * future AI→TE command bridge. On load we push each bus name down onto the
 * matching WaveInputDevice's alias — a cheap rename, no audio-stream restart.
 * Channel REGROUPING (mono↔stereo via WaveDeviceDescription) is a later slice;
 * this slice binds by alias only.
 *
 * ── Design invariants ─────────────────────────────────────────────────────────
 *   - All bus-definition mutations route through editManager.getUndoManager()
 *     and begin their own transaction (I-02) — each is individually undoable.
 *   - applyToEngine() is a DERIVED side effect (setAlias on engine-level device
 *     objects); it is NOT undoable and is always re-derivable from the tree.
 *   - The hardware anchor is the first channel's indexInDevice (stable across
 *     sessions for a given interface), NOT a device pointer or list position.
 *
 * Construct one per application, alongside MixerEngine (see MainComponent).
 */
class InputBusManager
{
public:
    explicit InputBusManager (EditManager& editManager);

    //==============================================================================
    /** A named input bus: a semantic name over one or two hardware channels. */
    struct InputBus
    {
        juce::String name;          ///< Display name; becomes the WaveInputDevice alias.
        juce::String role;          ///< Semantic role for Phase 3 AI ("vocal"/"drums"/…). Empty for now.
        int          hwChannel = -1;///< First hardware channel (ChannelIndex::indexInDevice). -1 = unassigned.
        bool         stereo    = false; ///< true = spans hwChannel and hwChannel+1.

        bool isValid() const noexcept { return name.isNotEmpty() && hwChannel >= 0; }
    };

    //==============================================================================
    // Reads (const, never mutate the tree)
    //==============================================================================

    /** Number of defined buses (0 if no edit or no <INPUTBUSES> tree yet). */
    int getNumBuses() const;

    /** Returns all defined buses in declaration order. */
    juce::Array<InputBus> getBuses() const;

    /** Returns the bus at index, or an invalid InputBus if out of range. */
    InputBus getBus (int index) const;

    /** Index of the first bus with the given name, or -1 if none. */
    int indexOfBusNamed (const juce::String& name) const;

    /** Index of the first bus mapped to the given hardware channel, or -1 if none.
     *  Used to find a track's bus from its assigned WaveInputDevice's first channel. */
    int indexOfBusForChannel (int hwChannel) const;

    //==============================================================================
    // CRUD — each begins its own undo transaction
    //==============================================================================

    /** Create a new named bus mapped to hwChannel (+1 if stereo). Undoable. */
    void createBus (const juce::String& name, int hwChannel, bool stereo);

    /** Rename the bus at index. Re-applies the alias to the engine. Undoable. */
    void renameBus (int index, const juce::String& newName);

    /** Remap the bus at index to a different hardware channel / width. Undoable. */
    void remapBus (int index, int hwChannel, bool stereo);

    /** Delete the bus at index. Undoable. */
    void deleteBus (int index);

    //==============================================================================
    // Engine binding
    //==============================================================================

    /** Resolve a bus to the live WaveInputDevice whose first channel matches
     *  bus.hwChannel, or nullptr if that channel is not present on the current
     *  interface (bus is "offline" — its name is still preserved in the session). */
    te::WaveInputDevice* resolveBusToDevice (const InputBus& bus) const;

    /** Convenience overload by bus index. */
    te::WaveInputDevice* resolveBusToDevice (int index) const;

    /** Push every bus name onto its matching WaveInputDevice alias (the hybrid
     *  binding step). Cheap, idempotent, no stream restart. Call after loading or
     *  creating an Edit and after any bus rename/remap. */
    void applyToEngine();

    /** If no buses exist yet (fresh session or a Phase 1 session with none),
     *  seed one mono bus per available hardware input named after the device.
     *  Automatic migration — NOT recorded in undo history. */
    void initialiseDefaultBuses();

private:
    EditManager& editManager;

    /** Returns (creating if absent) the <INPUTBUSES> child of the edit state.
     *  Returns an invalid tree if there is no current edit. */
    juce::ValueTree getOrCreateBusesTree();

    /** Const read of the <INPUTBUSES> tree — returns an invalid (empty) tree if
     *  absent, so getNumChildren() == 0. Never creates. */
    juce::ValueTree readBusesTree() const;

    /** Parse a single <INPUTBUS> node into an InputBus struct. */
    static InputBus busFromNode (const juce::ValueTree& node);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputBusManager)
};

//==============================================================================
// ValueTree schema identifiers — shared with the Bus-Manager UI and the future
// AI bridge. <INPUTBUSES> is a child of edit->state (round-trips with the session).
//==============================================================================

/** Container node: <INPUTBUSES> child of the Edit state. */
static const juce::Identifier kInputBusesTree { "INPUTBUSES" };
/** Per-bus node: <INPUTBUS> child of <INPUTBUSES>. */
static const juce::Identifier kInputBusNode   { "INPUTBUS" };
/** Bus properties. */
static const juce::Identifier kInputBusName      { "name" };
static const juce::Identifier kInputBusRole      { "role" };
static const juce::Identifier kInputBusHwChannel { "hwChannel" };
static const juce::Identifier kInputBusStereo    { "stereo" };
