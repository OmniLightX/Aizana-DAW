#include "PluginWindowManager.h"
#include "plugins/oop/OutOfProcessPluginInstance.h"

namespace
{
    aizana::OutOfProcessPluginInstance* remoteInstance (te::Plugin& plugin)
    {
        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            return dynamic_cast<aizana::OutOfProcessPluginInstance*> (external->getAudioPluginInstance());
        return nullptr;
    }
}


//==============================================================================

PluginWindowManager::PluginWindowManager (juce::Component& mainParent_)
    : mainParent (mainParent_)
{
}

PluginWindowManager::~PluginWindowManager()
{
    // FloatingPluginWindows are DocumentWindows — they must be closed on the
    // message thread.  Since PluginWindowManager is owned by MainComponent
    // (a message-thread object), destruction happens on the message thread.
    // Do not resize mainParent from its member's destructor.
    for (const auto& entry : remotePlugins)
        if (auto* remote = remoteInstance (*entry.second))
            remote->hideChildGui();
    remotePlugins.clear();
    floatingWindows.clear();
}

//==============================================================================
// Public interface — thread-safe: post to message thread

void PluginWindowManager::openPluginWindow (te::Plugin& plugin)
{
    juce::MessageManager::callAsync ([this, &plugin]
    {
        openFloatingWindow_msgThread (plugin);
    });
}

void PluginWindowManager::pinPluginToSidePanel (te::Plugin& plugin)
{
    juce::MessageManager::callAsync ([this, &plugin]
    {
        pinPlugin_msgThread (plugin);
    });
}

void PluginWindowManager::unpinPlugin (te::Plugin& plugin)
{
    juce::MessageManager::callAsync ([this, &plugin]
    {
        unpinPlugin_msgThread (plugin);
    });
}

void PluginWindowManager::closePluginWindow (te::Plugin& plugin)
{
    juce::MessageManager::callAsync ([this, &plugin]
    {
        closeWindow_msgThread (plugin);
    });
}

void PluginWindowManager::closePluginWindowNow(te::Plugin& plugin)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    closeWindow_msgThread(plugin);
}

bool PluginWindowManager::isPluginWindowOpen (const te::Plugin& plugin) const
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (remotePlugins.count (const_cast<te::Plugin*> (&plugin)) > 0)
        if (auto* remote = remoteInstance (const_cast<te::Plugin&> (plugin)))
            return remote->isChildGuiVisible();

    auto it = floatingWindows.find (const_cast<te::Plugin*>(&plugin));
    if (it != floatingWindows.end() && it->second != nullptr)
        return true;

    return dockedPlugins.count (const_cast<te::Plugin*>(&plugin)) > 0;
}

//==============================================================================
// Internal helpers — message thread only

void PluginWindowManager::openFloatingWindow_msgThread (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        external->initialiseFully();
    if (auto* remote = remoteInstance (plugin); remote != nullptr && remote->hasEditor())
    {
        remotePlugins[&plugin] = te::Plugin::Ptr (&plugin);
        remote->showChildGui();
        return;
    }

    // If already floating, bring to front
    auto it = floatingWindows.find (&plugin);
    if (it != floatingWindows.end() && it->second != nullptr)
    {
        it->second->toFront (true);
        return;
    }

    // If already docked, unpin first
    if (dockedPlugins.count (&plugin) > 0)
    {
        unpinPlugin_msgThread (plugin);
        return;  // unpinPlugin_msgThread will open floating window
    }

    // Create a new floating window
    floatingWindows[&plugin] = std::make_unique<FloatingPluginWindow> (plugin, *this);
}

void PluginWindowManager::pinPlugin_msgThread (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // A native window in another process cannot be embedded in our dock.
    if (auto* remote = remoteInstance (plugin); remote != nullptr && remote->hasEditor())
    {
        openFloatingWindow_msgThread (plugin);
        return;
    }

    // Close floating window if it exists
    auto it = floatingWindows.find (&plugin);
    if (it != floatingWindows.end())
    {
        it->second.reset();
        floatingWindows.erase (it);
    }

    // Add to docked panel
    if (dockedPlugins.count (&plugin) == 0)
    {
        dockedPlugins.insert (&plugin);
        dockedPanel.addPlugin (plugin);

        // Notify parent to resize (DockedPluginPanel needs to appear)
        if (auto* parent = dockedPanel.getParentComponent())
            parent->resized();
    }
}

void PluginWindowManager::unpinPlugin_msgThread (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Remove from docked panel
    if (dockedPlugins.count (&plugin) > 0)
    {
        dockedPlugins.erase (&plugin);
        dockedPanel.removePlugin (plugin);

        // Notify parent to resize (DockedPluginPanel may now be empty/hidden)
        if (auto* parent = dockedPanel.getParentComponent())
            parent->resized();
    }

    // Open as floating window
    floatingWindows[&plugin] = std::make_unique<FloatingPluginWindow> (plugin, *this);
}

void PluginWindowManager::closeAllWindows()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    for (const auto& entry : remotePlugins)
        if (auto* remote = remoteInstance (*entry.second))
            remote->hideChildGui();
    remotePlugins.clear();
    floatingWindows.clear();

    // Copy — DockedPluginPanel::removePlugin must not invalidate our iteration
    const auto docked = dockedPlugins;
    for (auto* p : docked)
        dockedPanel.removePlugin (*p);
    dockedPlugins.clear();

    if (auto* parent = dockedPanel.getParentComponent())
        parent->resized();
}

void PluginWindowManager::closeWindow_msgThread (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (auto it = remotePlugins.find (&plugin); it != remotePlugins.end())
    {
        if (auto* remote = remoteInstance (plugin))
            remote->hideChildGui();
        remotePlugins.erase (it);
    }

    // Remove floating window
    auto it = floatingWindows.find (&plugin);
    if (it != floatingWindows.end())
    {
        it->second.reset();
        floatingWindows.erase (it);
    }

    // Remove from docked panel
    if (dockedPlugins.count (&plugin) > 0)
    {
        dockedPlugins.erase (&plugin);
        dockedPanel.removePlugin (plugin);

        if (auto* parent = dockedPanel.getParentComponent())
            parent->resized();
    }
}
