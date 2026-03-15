#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class PluginWindowManager;

/**
 * FloatingPluginWindow — A DocumentWindow hosting a plugin's native editor.
 *
 * Created on the message thread only (enforced by assertion in PluginWindowManager).
 * The title bar contains a pin button — clicking it moves the editor to the
 * DockedPluginPanel via PluginWindowManager::pinPluginToSidePanel().
 *
 * Lifetime: owned by PluginWindowManager via std::unique_ptr.
 * Destroyed when user closes window or plugin is removed from chain.
 */
class FloatingPluginWindow : public juce::DocumentWindow
{
public:
    FloatingPluginWindow (te::Plugin& plugin, PluginWindowManager& manager);
    ~FloatingPluginWindow() override;

    /** Called when user clicks the native window close button. */
    void closeButtonPressed() override;

    te::Plugin& getPlugin() const noexcept { return plugin; }

private:
    te::Plugin&           plugin;
    PluginWindowManager&  manager;

    // Custom "pin" button added to the title bar
    class PinButton : public juce::Button
    {
    public:
        explicit PinButton (FloatingPluginWindow& owner);
        void paintButton (juce::Graphics& g, bool mouseOver, bool isDown) override;
        void clicked() override;
    private:
        FloatingPluginWindow& owner;
    };

    std::unique_ptr<PinButton> pinButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPluginWindow)
};
