#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "PluginScanner.h"
#include "../engine/EditManager.h"

namespace te = tracktion::engine;

/**
 * PluginManager — Plugin insertion, chain reordering, and removal via EditManager.
 *
 * Wraps Tracktion Engine's PluginList to provide:
 *   - Insert with undo support (PLUG-04)
 *   - Move with undo support (chain order = ValueTree child order)
 *   - Remove with undo support
 *
 * Design invariants:
 *   - Every mutation uses EditManager::getUndoManager() — never a separate one.
 *   - Plugin chain order in the PluginList ALWAYS matches ValueTree child order.
 *     This is a Tracktion Engine guarantee — PluginList is backed by ValueTreeObjectList.
 *   - movePlugin() manipulates the PluginList::state ValueTree directly via
 *     ValueTree::moveChild(), which is the correct way to reorder since PluginList
 *     has no movePlugin() method.
 */
class PluginManager
{
public:
    PluginManager (PluginScanner& scanner, EditManager& editManager);
    ~PluginManager() = default;

    // ---------------------------------------------------------------------------
    // PLUG-04: Insert chain management
    // ---------------------------------------------------------------------------

    /**
     * Create and insert a plugin from a PluginDescription into a track's chain.
     *
     * @param track       The target track.
     * @param desc        The JUCE PluginDescription from KnownPluginList.
     * @param insertIndex Position in the chain (0-based). Pass -1 to append at the end.
     * @return            The created Plugin, or nullptr on failure.
     *
     * Records one undo step: "Insert plugin".
     */
    te::Plugin* insertPlugin (te::Track& track,
                              const juce::PluginDescription& desc,
                              int insertIndex = -1);

    /**
     * Create and insert a plugin into a CLIP's own chain (Event-FX).
     *
     * TE processes a clip's PluginList live as part of the clip's playback
     * node — the FX exists only while this event plays (Studio One Event-FX).
     * Returns nullptr for clip types without a plugin list.
     *
     * Records one undo step: "Insert event FX".
     */
    te::Plugin* insertPluginOnClip (te::Clip& clip,
                                    const juce::PluginDescription& desc);

    /**
     * Move a plugin to a new position in the track's chain.
     *
     * The chain order is ValueTree child order — moving the plugin's state node
     * in the ValueTree updates both the PluginList ordering and the audio graph.
     *
     * @param track     The track owning the plugin.
     * @param plugin    The plugin to move.
     * @param newIndex  Destination position (0-based).
     *
     * Records one undo step: "Move plugin in chain".
     */
    void movePlugin (te::Track& track, te::Plugin& plugin, int newIndex);

    /**
     * Remove a plugin from a track's chain.
     *
     * @param track   The track owning the plugin.
     * @param plugin  The plugin to remove.
     *
     * Records one undo step: "Remove plugin".
     */
    void removePlugin (te::Track& track, te::Plugin& plugin);

    /**
     * Move a plugin between USER-VISIBLE slot positions (mixer insert slots).
     *
     * Slots index only user plugins — internal TE plugins (volume, level,
     * auxreturn, auxsend) are hidden from the slot list but present in the raw
     * chain, possibly interleaved with user plugins. This computes the correct
     * raw ValueTree target index so the plugin lands at visible position
     * `toSlot` regardless of where the internal plugins sit.
     *
     * toSlot is clamped to the user-plugin range; passing the trailing "+"
     * slot index moves the plugin to the end of the user block.
     *
     * Records one undo step (via movePlugin).
     */
    void moveUserPluginToSlot (te::Track& track, int fromSlot, int toSlot);

    // ---------------------------------------------------------------------------
    // Chain query
    // ---------------------------------------------------------------------------

    /**
     * Returns the track's plugin chain in processing order.
     * Order matches ValueTree child order (Tracktion Engine guarantee).
     */
    juce::Array<te::Plugin*> getPluginChain (te::Track& track) const;

    /**
     * Map a user-visible insert-slot index to the raw chain index for insertPlugin().
     *
     * The mixer's slot list hides internal TE plugins (volume, level, auxreturn,
     * auxsend), but those can sit ANYWHERE in the raw chain — e.g. a default track
     * is [volume, level] and appended user plugins follow them, so slot N is NOT
     * raw index N. Inserting at `prefix + slotIndex` (the old scheme) placed new
     * plugins between existing user plugins whenever internals preceded them.
     *
     * Returns the raw index of the user plugin currently occupying `userSlot`
     * (insert lands before it = takes that visible position). For the trailing
     * "+" slot it returns the index just after the last user plugin. With no
     * user plugins it returns the index after any leading auxreturn/auxsend
     * prefix (FX-channel behaviour preserved).
     */
    int chainInsertIndexForUserSlot (te::Track& track, int userSlot) const;

private:
    PluginScanner& scanner;
    EditManager&   editManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
