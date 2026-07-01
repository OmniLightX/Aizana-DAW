#include "PluginManager.h"
#include "PluginLoadLog.h"
#include "oop/OutOfProcessPluginInstance.h"

namespace
{
    // E5.3c: with CLOUDES_OOP_PLUGINS=1, route Cloudes-generated CLAP plugins through the
    // out-of-process host by rewriting the format name (TE then picks OutOfProcessPluginFormat).
    // Flag OFF (default) → returns the description unchanged, so the shipping path is untouched.
    juce::PluginDescription maybeRouteOutOfProcess (const juce::PluginDescription& desc)
    {
        if (cloudes::oopHostingEnabled()
            && desc.manufacturerName == "Cloudes"
            && desc.pluginFormatName == "CLAP")
        {
            auto d = desc;
            d.pluginFormatName = cloudes::kOopFormatName;
            return d;
        }
        return desc;
    }
}

PluginManager::PluginManager (PluginScanner& scanner, EditManager& editManager)
    : scanner     (scanner)
    , editManager (editManager)
{}

// ---------------------------------------------------------------------------
// Insert
// ---------------------------------------------------------------------------

te::Plugin* PluginManager::insertPlugin (te::Track& track,
                                          const juce::PluginDescription& descIn,
                                          int insertIndex)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    const auto desc = maybeRouteOutOfProcess (descIn);

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Insert plugin");

    CloudesPluginLog::stage ("insertPlugin BEGIN  name=\"" + desc.name + "\"  format="
                             + desc.pluginFormatName + "  manuf=" + desc.manufacturerName);

    // TE's ExternalPlugin resolves its real instance from the ENGINE's knownPluginList
    // (ExternalPlugin::findMatchingPlugin), NOT from Cloudes' scanner list. Without this,
    // third-party plugins load as empty shells: getAudioPluginInstance() is null, so the
    // editor shows "No editor available" and no audio is processed. Register the
    // description with the engine first so doFullInitialisation() creates the instance.
    edit->engine.getPluginManager().knownPluginList.addType (desc);
    CloudesPluginLog::stage ("insertPlugin  addType done, calling createNewPlugin...");

    // Create a new plugin via Tracktion Engine's PluginCache.
    // createNewPlugin wraps the JUCE PluginDescription into a tracktion Plugin::Ptr.
    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);
    CloudesPluginLog::stage ("insertPlugin  createNewPlugin returned "
                             + juce::String (pluginPtr != nullptr ? "OK" : "NULL"));

    if (pluginPtr == nullptr)
        return nullptr;

    track.pluginList.insertPlugin (pluginPtr, insertIndex, nullptr);
    CloudesPluginLog::stage ("insertPlugin DONE  \"" + desc.name + "\"");

    return pluginPtr.get();
}

te::Plugin* PluginManager::insertPluginOnClip (te::Clip& clip,
                                               const juce::PluginDescription& desc)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto* clipPlugins = clip.getPluginList();
    if (clipPlugins == nullptr)
        return nullptr;   // clip type without a plugin chain

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Insert event FX");

    CloudesPluginLog::stage ("insertPluginOnClip BEGIN  name=\"" + desc.name + "\"");

    // Same engine registration as the track path — see insertPlugin().
    edit->engine.getPluginManager().knownPluginList.addType (desc);

    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);
    if (pluginPtr == nullptr)
        return nullptr;

    clipPlugins->insertPlugin (pluginPtr, -1, nullptr);   // append at end
    CloudesPluginLog::stage ("insertPluginOnClip DONE  \"" + desc.name + "\"");

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
