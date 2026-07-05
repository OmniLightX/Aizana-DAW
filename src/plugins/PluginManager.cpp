#include "PluginManager.h"
#include "PluginLoadLog.h"
#include "oop/OutOfProcessPluginInstance.h"

namespace
{
    // E5.3c: with AIZANA_OOP_PLUGINS=1, route Aizana-generated CLAP plugins through the
    // out-of-process host by rewriting the format name (TE then picks OutOfProcessPluginFormat).
    // Flag OFF (default) → returns the description unchanged, so the shipping path is untouched.
    juce::PluginDescription maybeRouteOutOfProcess (const juce::PluginDescription& desc)
    {
        if (aizana::oopHostingEnabled()
            && desc.manufacturerName == "Aizana"
            && desc.pluginFormatName == "CLAP")
        {
            auto d = desc;
            d.pluginFormatName = aizana::kOopFormatName;
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

    AizanaPluginLog::stage ("insertPlugin BEGIN  name=\"" + desc.name + "\"  format="
                             + desc.pluginFormatName + "  manuf=" + desc.manufacturerName);

    // TE's ExternalPlugin resolves its real instance from the ENGINE's knownPluginList
    // (ExternalPlugin::findMatchingPlugin), NOT from Aizana' scanner list. Without this,
    // third-party plugins load as empty shells: getAudioPluginInstance() is null, so the
    // editor shows "No editor available" and no audio is processed. Register the
    // description with the engine first so doFullInitialisation() creates the instance.
    edit->engine.getPluginManager().knownPluginList.addType (desc);
    AizanaPluginLog::stage ("insertPlugin  addType done, calling createNewPlugin...");

    // Create a new plugin via Tracktion Engine's PluginCache.
    // createNewPlugin wraps the JUCE PluginDescription into a tracktion Plugin::Ptr.
    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);
    AizanaPluginLog::stage ("insertPlugin  createNewPlugin returned "
                             + juce::String (pluginPtr != nullptr ? "OK" : "NULL"));

    if (pluginPtr == nullptr)
        return nullptr;

    track.pluginList.insertPlugin (pluginPtr, insertIndex, nullptr);
    AizanaPluginLog::stage ("insertPlugin DONE  \"" + desc.name + "\"");

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

    AizanaPluginLog::stage ("insertPluginOnClip BEGIN  name=\"" + desc.name + "\"");

    // Same engine registration as the track path — see insertPlugin().
    edit->engine.getPluginManager().knownPluginList.addType (desc);

    auto pluginPtr = edit->getPluginCache().createNewPlugin (
        te::ExternalPlugin::xmlTypeName, desc);
    if (pluginPtr == nullptr)
        return nullptr;

    clipPlugins->insertPlugin (pluginPtr, -1, nullptr);   // append at end
    AizanaPluginLog::stage ("insertPluginOnClip DONE  \"" + desc.name + "\"");

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

    // Plugin chain order == relative order of the PLUGIN child nodes.
    // Moving the plugin's state node in the ValueTree is the correct way to
    // reorder — PluginList does not have a movePlugin() method; it rebuilds
    // its object list from the ValueTree on every change.
    //
    // CAUTION: pluginList.state also holds non-PLUGIN children, so PluginList
    // indices and raw ValueTree child indices do NOT map 1:1 — newIndex (a
    // PluginList index) must be translated to a tree index first. (Found via
    // the M4 reorder test: passing the list index moved plugins to the wrong
    // spot whenever non-PLUGIN nodes preceded the PLUGIN block.)
    auto& listState = track.pluginList.state;

    // Tree indices of all PLUGIN children, in chain order
    juce::Array<int> pluginTreeIdx;
    for (int i = 0; i < listState.getNumChildren(); ++i)
        if (listState.getChild (i).hasType (te::IDs::PLUGIN))
            pluginTreeIdx.add (i);

    const int fromTree = listState.indexOf (plugin.state);
    const int fromList = pluginTreeIdx.indexOf (fromTree);
    if (fromList < 0)
        return; // Plugin not found in this track's list

    // Tree indices of the remaining PLUGIN nodes as they will be after the
    // removal — ValueTree::moveChild() uses post-removal indices.
    juce::Array<int> reduced;
    for (int i = 0; i < pluginTreeIdx.size(); ++i)
    {
        if (i == fromList)
            continue;
        const int t = pluginTreeIdx[i];
        reduced.add (t > fromTree ? t - 1 : t);
    }

    newIndex = juce::jlimit (0, reduced.size(), newIndex);
    const int toTree = newIndex < reduced.size()
                           ? reduced[newIndex]
                           : (reduced.isEmpty() ? fromTree : reduced.getLast() + 1);

    listState.moveChild (fromTree, toTree, &undoManager);
}

void PluginManager::moveUserPluginToSlot (te::Track& track, int fromSlot, int toSlot)
{
    // Collect user plugins with their raw chain indices (same filter as the
    // mixer slot list — internals stay where they are).
    const auto chain = getPluginChain (track);
    juce::Array<te::Plugin*> user;
    juce::Array<int>         rawIdx;

    for (int i = 0; i < chain.size(); ++i)
    {
        const juce::String t = chain[i]->getPluginType();
        if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
        {
            user.add (chain[i]);
            rawIdx.add (i);
        }
    }

    if (fromSlot < 0 || fromSlot >= user.size())
        return;

    toSlot = juce::jlimit (0, user.size() - 1, toSlot);
    if (toSlot == fromSlot)
        return;

    const int rawFrom = rawIdx[fromSlot];

    // Raw indices of the remaining user plugins as they will be AFTER the moved
    // plugin's removal — ValueTree::moveChild() uses post-removal indices.
    juce::Array<int> rawReduced;
    for (int i = 0; i < user.size(); ++i)
    {
        if (i == fromSlot)
            continue;
        const int r = rawIdx[i];
        rawReduced.add (r > rawFrom ? r - 1 : r);
    }

    const int to = toSlot < rawReduced.size()
                       ? rawReduced[toSlot]
                       : (rawReduced.isEmpty() ? rawFrom : rawReduced.getLast() + 1);

    movePlugin (track, *user[fromSlot], to);
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

int PluginManager::chainInsertIndexForUserSlot (te::Track& track, int userSlot) const
{
    const auto chain = getPluginChain (track);

    int  userSeen        = 0;
    int  afterLastUser   = -1;
    int  auxPrefixEnd    = 0;
    bool inLeadingPrefix = true;

    for (int i = 0; i < chain.size(); ++i)
    {
        const juce::String t = chain[i]->getPluginType();

        if (t == "auxreturn" || t == "auxsend")
        {
            if (inLeadingPrefix)
                auxPrefixEnd = i + 1;
            continue;
        }

        if (t == "volume" || t == "level")
            continue;   // internal — invisible to slots, skip wherever it sits

        inLeadingPrefix = false;

        if (userSeen == userSlot)
            return i;   // insert before the plugin occupying this slot

        ++userSeen;
        afterLastUser = i + 1;
    }

    // Trailing "+" slot (or slot beyond the last user plugin): append after the
    // user block; with no user plugins, land after the aux prefix (FX channels).
    return afterLastUser >= 0 ? afterLastUser : auxPrefixEnd;
}
