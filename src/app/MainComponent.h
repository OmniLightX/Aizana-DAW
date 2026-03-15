#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ApplicationState.h"
#include "engine/MixerEngine.h"
#include "engine/AutomationEngine.h"
#include "engine/ClipEditor.h"
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
 */
class MainComponent : public juce::Component
{
public:
    explicit MainComponent (ApplicationState& appState);

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Access the PluginWindowManager for opening plugin editor windows. */
    PluginWindowManager& getPluginWindowManager() noexcept { return pluginWindowManager; }

private:
    ApplicationState& appState;

    // Engine objects — owned by MainComponent, shared with UI components
    MixerEngine      mixerEngine      { appState.editManager };
    AutomationEngine automationEngine { appState.editManager };
    ClipEditor       clipEditor       { appState.editManager };

    // Plugin window management (plan 01-10)
    PluginWindowManager pluginWindowManager { *this };

    // UI components
    TransportBar    transportBar    { appState };
    ArrangementView arrangementView { appState, clipEditor, mixerEngine };
    MixerView       mixerView       { appState, mixerEngine };
    // dockedPanel is inside pluginWindowManager — added to our layout via getDockedPanel()

    bool mixerVisible = true;   // CONTEXT.md: mixer visible by default

    static constexpr int kDockedPanelWidth = 350;  // px when plugins are docked

    void toggleMixer();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
