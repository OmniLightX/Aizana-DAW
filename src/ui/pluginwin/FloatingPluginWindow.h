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
 * Fullscreen prevention: setFullScreen()/isFullScreen() are overridden so that
 * the plugin window never participates in macOS fullscreen transitions even when
 * the main Aizana window enters fullscreen mode.
 *
 * Zoom: a bottom bar inside BypassWrapper provides 80 / 100 / 120 / 150 % zoom
 * buttons.  applyZoom() resizes the window so the editor fills the new size.
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

    /**
     * Transport-Tasten auch mit offenem Plugin-Fenster.
     *
     * Ohne das tat die Leertaste hier NICHTS ausser einem Systemton: der Fokus
     * liegt beim Plugin-Fenster, die Taste erreicht MainComponent nie, und
     * macOS quittiert eine unbeantwortete Taste mit NSBeep. Wer im EQ arbeitet
     * und stoppen will, glaubt dann, die App haenge — dabei laeuft die
     * Wiedergabe einfach weiter (User-Befund 2026-08-15).
     *
     * Greift nur, wenn kein Textfeld den Fokus hat: sonst koennte man in einem
     * Plugin kein Leerzeichen mehr tippen.
     */
    bool keyPressed (const juce::KeyPress& key) override;

    te::Plugin& getPlugin() const noexcept { return plugin; }

    // ---- Fullscreen prevention ----
    // Override maximiseButtonPressed so the (optional) maximise button never
    // sends the plugin window into fullscreen — it applies 150 % zoom instead.
    void maximiseButtonPressed() override { applyZoom (1.5f); }

    // ---- Zoom ----
    /** Resize the window so the editor fills nativeW*factor × nativeH*factor. */
    void applyZoom (float factor);

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

    // Native editor size (before zoom) — set during construction.
    juce::int64 lastSpaceKeyTimeMs = 0;

    int nativeEditorW = 400;
    int nativeEditorH = 300;
    float currentZoom = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingPluginWindow)
};
