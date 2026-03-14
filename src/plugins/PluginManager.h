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

    // ---------------------------------------------------------------------------
    // Chain query
    // ---------------------------------------------------------------------------

    /**
     * Returns the track's plugin chain in processing order.
     * Order matches ValueTree child order (Tracktion Engine guarantee).
     */
    juce::Array<te::Plugin*> getPluginChain (te::Track& track) const;

private:
    PluginScanner& scanner;
    EditManager&   editManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
