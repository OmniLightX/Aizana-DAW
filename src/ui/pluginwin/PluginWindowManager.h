#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <set>

#include "FloatingPluginWindow.h"
#include "DockedPluginPanel.h"

namespace te = tracktion::engine;

/**
 * PluginWindowManager — Central registry for all open plugin windows.
 *
 * Manages two plugin display modes (CONTEXT.md: floating by default, pinnable):
 *   1. FloatingPluginWindow  — per-plugin DocumentWindow (default)
 *   2. DockedPluginPanel     — right-side panel stacking multiple editors
 *
 * Both modes can be active simultaneously: some plugins float, some are docked.
 *
 * Thread safety:
 *   - ALL window creation is posted to the message thread via
 *     juce::MessageManager::callAsync (RESEARCH.md Pitfall 7).
 *   - openPluginWindow() and pinPluginToSidePanel() are safe to call
 *     from any thread; they post the actual work to the message thread.
 *   - isPluginWindowOpen() is safe to call from the message thread only.
 *
 * Lifecycle:
 *   - Owned by MainComponent.
 *   - getDockedPanel() returns a reference — add it to MainComponent's layout.
 *   - closePluginWindow() is called when a plugin is removed from a chain.
 */
class PluginWindowManager
{
public:
    /**
     * @param mainParent  The parent component for floating windows
     *                    (used for display bounds). Typically the desktop.
     */
    explicit PluginWindowManager (juce::Component& mainParent);
    ~PluginWindowManager();

    // -------------------------------------------------------------------------
    // Window management
    // -------------------------------------------------------------------------

    /**
     * Opens a floating window for this plugin.
     * If a window is already open (floating or docked), brings it to front.
     * Thread-safe: posts window creation to the message thread.
     */
    void openPluginWindow (te::Plugin& plugin);

    /**
     * Pins plugin to the docked side panel.
     * If a floating window exists for this plugin, it is closed first.
     * Thread-safe: posts to message thread.
     */
    void pinPluginToSidePanel (te::Plugin& plugin);

    /**
     * Unpins a plugin from the docked panel and opens it as a floating window.
     * Thread-safe: posts to message thread.
     */
    void unpinPlugin (te::Plugin& plugin);

    /**
     * Closes any window for this plugin (floating or docked).
     * Called when plugin is removed from a chain.
     * Thread-safe: posts to message thread.
     */
    void closePluginWindow (te::Plugin& plugin);

    // Before list mutations destroy a plugin: close editors on the message thread.
    void closePluginWindowNow(te::Plugin& plugin);

    /**
     * Returns true if any window (floating or docked) exists for this plugin.
     * Call from message thread only.
     */
    bool isPluginWindowOpen (const te::Plugin& plugin) const;

    /**
     * Closes ALL plugin windows (floating + docked). MUST be called on an Edit
     * swap (File>New/Open) while the old Edit is still alive — every window
     * holds a te::Plugin& into the old Edit. Call from message thread only.
     */
    void closeAllWindows();

    // -------------------------------------------------------------------------
    // Panel access
    // -------------------------------------------------------------------------

    /** Returns the docked side panel component (add to MainComponent layout). */
    DockedPluginPanel& getDockedPanel() noexcept { return dockedPanel; }

private:
    juce::Component&  mainParent;
    DockedPluginPanel dockedPanel;

    // Floating windows — keyed by plugin pointer
    std::map<te::Plugin*, std::unique_ptr<FloatingPluginWindow>> floatingWindows;

    // Keep remote plugins alive until close/removal/session swap. They own their
    // window in the worker; there is deliberately no host DocumentWindow.
    std::map<te::Plugin*, te::Plugin::Ptr> remotePlugins;

    // Set of plugins currently in the docked panel
    std::set<te::Plugin*> dockedPlugins;

    // -------------------------------------------------------------------------
    // Internal helpers — always called on the message thread
    // -------------------------------------------------------------------------

    void openFloatingWindow_msgThread (te::Plugin& plugin);
    void pinPlugin_msgThread          (te::Plugin& plugin);
    void unpinPlugin_msgThread        (te::Plugin& plugin);
    void closeWindow_msgThread        (te::Plugin& plugin);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindowManager)
};
