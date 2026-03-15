#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ApplicationState.h"
#include "engine/MixerEngine.h"
#include "engine/AutomationEngine.h"
#include "engine/ClipEditor.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/arrangement/TransportBar.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/mixer/MixerView.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "ui/preferences/PreferencesDialog.h"

/**
 * MainComponent — Root JUCE Component for the Cloudes DAW window.
 *
 * Four-region layout (CONTEXT.md + plan 01-10):
 *   1. TransportBar      — fixed 50px at top
 *   2. ArrangementView   — timeline, fills remaining upper area
 *   3. MixerView         — horizontal channel strips at bottom (~35% of remaining height)
 *   4. DockedPluginPanel — collapsible right-side panel (hidden when empty)
 *
 * MixerView is visible by default (mixing-first per CONTEXT.md).
 * Cmd+M (macOS) / Ctrl+M (Windows) toggles MixerView visibility.
 *
 * DockedPluginPanel width is 350px when plugins are docked, 0px when empty.
 *
 * Engine objects (MixerEngine, AutomationEngine, ClipEditor) are owned here
 * and shared by reference with child components that need them.
 *
 * Plan 01-13: MainComponent now implements juce::MenuBarModel, providing
 * File, Edit, View, and Export menus. MainWindow wires this to the native
 * macOS system menu bar (setMacMainMenu) or an in-window MenuBarComponent
 * on Windows/Linux.
 *
 * Plan 01-15: MainComponent owns PluginScanner and PluginManager. Both are
 * passed down to ArrangementView → TrackHeader for the right-click plugin
 * insert workflow.
 */
class MainComponent : public juce::Component,
                      public juce::MenuBarModel
{
public:
    explicit MainComponent (ApplicationState& appState);

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Access the PluginWindowManager for opening plugin editor windows. */
    PluginWindowManager& getPluginWindowManager() noexcept { return pluginWindowManager; }

    // -------------------------------------------------------------------------
    // juce::MenuBarModel overrides (pure virtual — all three must be implemented)
    // -------------------------------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    ApplicationState& appState;

    // Engine objects — owned by MainComponent, shared with UI components.
    // Declaration order matters: objects are initialised in declaration order.
    // PluginScanner has no dependencies on other members.
    // PluginManager depends on PluginScanner + EditManager (via appState).
    MixerEngine      mixerEngine      { appState.editManager };
    AutomationEngine automationEngine { appState.editManager };
    ClipEditor       clipEditor       { appState.editManager };

    // Plugin scanning + management (plan 01-15)
    // PluginScanner must be declared before PluginManager (dependency order)
    PluginScanner    pluginScanner;
    PluginManager    pluginManager    { pluginScanner, appState.editManager };

    // Plugin window management (plan 01-10)
    PluginWindowManager pluginWindowManager { *this };

    // UI components
    TransportBar    transportBar    { appState };
    ArrangementView arrangementView { appState, clipEditor, mixerEngine,
                                      pluginManager, pluginWindowManager, pluginScanner };
    MixerView       mixerView       { appState, mixerEngine };
    // dockedPanel is inside pluginWindowManager — added to our layout via getDockedPanel()

    bool mixerVisible = true;   // CONTEXT.md: mixer visible by default

    static constexpr int kDockedPanelWidth = 350;  // px when plugins are docked

    void toggleMixer();

    // -------------------------------------------------------------------------
    // Menu item IDs — enum avoids magic numbers and ensures uniqueness
    // -------------------------------------------------------------------------
    enum MenuItemIDs
    {
        // File menu
        FileNewSession  = 1,
        FileOpen        = 2,
        FileSave        = 3,
        FileSaveAs      = 4,
        FileImportAudio = 5,

        // Edit menu
        EditUndo = 101,
        EditRedo = 102,

        // View menu
        ViewToggleMixer = 201,

        // Export menu
        ExportMixdown = 301,
        ExportStems   = 302,
    };

    // Async file chooser — stored as member to keep alive across async callbacks
    std::unique_ptr<juce::FileChooser> fileChooser;

    // -------------------------------------------------------------------------
    // File menu handlers
    // -------------------------------------------------------------------------
    void handleFileNew();
    void handleFileOpen();
    void handleFileSave();
    void handleFileSaveAs();
    void handleFileImportAudio();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
