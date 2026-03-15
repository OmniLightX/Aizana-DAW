#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * DockedPluginPanel — Right-side panel that vertically stacks plugin editors.
 *
 * Plugins can be pinned here from their floating window via
 * PluginWindowManager::pinPluginToSidePanel(). Each editor is wrapped in a
 * header bar that contains the plugin name and an "unpin" button.
 *
 * Auto-hides when empty: isEmpty() returns true → MainComponent sets width 0.
 * Scrollable when many plugins are docked.
 *
 * Layout decision: each docked plugin gets a fixed height of 250px in the
 * vertical stack, horizontally fills the panel. User can scroll vertically.
 */
class DockedPluginPanel : public juce::Component
{
public:
    DockedPluginPanel();
    ~DockedPluginPanel() override;

    /**
     * Add a plugin's editor to the vertical stack.
     * The panel creates an editor via plugin.createEditorIfNeeded().
     * MUST be called on the message thread.
     */
    void addPlugin (te::Plugin& plugin);

    /**
     * Remove a plugin's editor from the vertical stack.
     * Called when user unpins, or plugin is removed from chain.
     */
    void removePlugin (te::Plugin& plugin);

    /** Returns true when no plugins are docked. */
    bool isEmpty() const;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    //==========================================================================
    // DockedEntry wraps one plugin's editor with a header bar
    struct DockedEntry : public juce::Component
    {
        te::Plugin& plugin;
        juce::Component* editor = nullptr;

        juce::TextButton unpinButton { "x" };
        juce::Label      pluginLabel;

        static constexpr int kHeaderH = 24;
        static constexpr int kEditorH = 250;
        static constexpr int kTotalH  = kHeaderH + kEditorH;

        std::function<void()> onUnpin;

        DockedEntry (te::Plugin& p, juce::Component* ed, std::function<void()> onUnpinFn);
        void resized() override;
        void paint (juce::Graphics& g) override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DockedEntry)
    };

    //==========================================================================
    juce::Viewport                        scrollViewport;
    juce::Component                       editorContainer;
    juce::OwnedArray<DockedEntry>         entries;

    void rebuildLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DockedPluginPanel)
};
