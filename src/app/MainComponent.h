#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ApplicationState.h"
#include "engine/MixerEngine.h"
#include "engine/AutomationEngine.h"
#include "engine/ClipEditor.h"
#include "ui/arrangement/TransportBar.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/mixer/MixerView.h"

/**
 * MainComponent — Root JUCE Component for the Cloudes DAW window.
 *
 * Three-panel layout (CONTEXT.md):
 *   1. TransportBar  — fixed 50px at top
 *   2. ArrangementView — timeline, fills remaining upper area
 *   3. MixerView — horizontal channel strips at bottom (~35% of remaining height)
 *
 * MixerView is visible by default (mixing-first per CONTEXT.md).
 * Cmd+M (macOS) / Ctrl+M (Windows) toggles MixerView visibility.
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

private:
    ApplicationState& appState;

    // Engine objects — owned by MainComponent, shared with UI components
    MixerEngine      mixerEngine      { appState.editManager };
    AutomationEngine automationEngine { appState.editManager };
    ClipEditor       clipEditor       { appState.editManager };

    // UI components
    TransportBar    transportBar    { appState };
    ArrangementView arrangementView { appState, clipEditor, mixerEngine };
    MixerView       mixerView       { appState, mixerEngine };

    bool mixerVisible = true;   // CONTEXT.md: mixer visible by default

    void toggleMixer();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
