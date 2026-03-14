#include "PluginManager.h"

PluginManager::PluginManager (PluginScanner& scanner, EditManager& editManager)
    : scanner     (scanner)
    , editManager (editManager)
{}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

te::Plugin* PluginManager::insertPlugin (te::Track& track,
                                          const juce::PluginDescription& desc,
                                          int insertIndex)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Insert plugin");

    // Create a new plugin via Tracktion Engine's PluginCache.
    // createNewPlugin wraps the JUCE PluginDescription into a tracktion Plugin::Ptr.
    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);

    if (pluginPtr == nullptr)
        return nullptr;

    track.pluginList.insertPlugin (pluginPtr, insertIndex, nullptr);

    return pluginPtr.get();
}

// ---------------------------------------------------------------------------
// Move
// ---------------------------------------------------------------------------

void PluginManager::movePlugin (te::Track& track, te::Plugin& plugin, int newIndex)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Move plugin in chain");

    // Plugin chain order == ValueTree child order.
    // Moving the plugin's state node in the ValueTree is the correct way to
    // reorder — PluginList does not have a movePlugin() method; it rebuilds
    // its object list from the ValueTree on every change.
    auto& listState = track.pluginList.state;

    const int currentTreeIndex = listState.indexOf (plugin.state);
    if (currentTreeIndex < 0)
        return; // Plugin not found in this track's list

    // Compute the ValueTree index that corresponds to PluginList index newIndex.
    // PluginList indices and ValueTree child indices may differ if there are
    // non-PLUGIN child nodes in the track state. In practice for a track's
    // pluginList.state the children are all PLUGIN nodes, so they map 1:1.
    listState.moveChild (currentTreeIndex, newIndex, &undoManager);
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

void PluginManager::removePlugin (te::Track& track, te::Plugin& plugin)
{
    juce::ignoreUnused (track);

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Remove plugin");

    // removeFromParent removes the plugin state node from its parent ValueTree
    // (with undo support via the edit's shared UndoManager) and also hides
    // any automation parameters and plugin windows.
    plugin.removeFromParent();
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

juce::Array<te::Plugin*> PluginManager::getPluginChain (te::Track& track) const
{
    juce::Array<te::Plugin*> chain;

    for (auto* plugin : track.pluginList)
        chain.add (plugin);

    return chain;
}
