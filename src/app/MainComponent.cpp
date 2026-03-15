#include "MainComponent.h"

//==============================================================================

MainComponent::MainComponent (ApplicationState& appState_)
    : appState (appState_)
{
    addAndMakeVisible (transportBar);
    addAndMakeVisible (arrangementView);
    addAndMakeVisible (mixerView);

    // DockedPluginPanel from PluginWindowManager — right-side panel
    addAndMakeVisible (pluginWindowManager.getDockedPanel());

    // Mixer visible by default (CONTEXT.md: mixing-first design)
    mixerView.setVisible (mixerVisible);

    // Allow this component to receive key events
    setWantsKeyboardFocus (true);

    setSize (1280, 800);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Fixed transport bar at top (CONTEXT.md: "Fixed transport bar at top")
    transportBar.setBounds (area.removeFromTop (50));

    // DockedPluginPanel — right-side panel (auto-hides when empty)
    auto& dockedPanel = pluginWindowManager.getDockedPanel();
    const int dockedWidth = dockedPanel.isEmpty() ? 0 : kDockedPanelWidth;
    dockedPanel.setBounds (area.removeFromRight (dockedWidth));

    if (mixerVisible)
    {
        // MixerView takes bottom ~35% of remaining area
        const int mixerHeight = (int)(area.getHeight() * 0.35f);
        mixerView.setBounds (area.removeFromBottom (mixerHeight));
    }

    // ArrangementView takes remaining upper area
    arrangementView.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // CONTEXT.md: Cmd+M (macOS) / Ctrl+M (Windows) toggles mixer
    if (key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0))
    {
        toggleMixer();
        return true;
    }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::ctrlModifier, 0))
    {
        toggleMixer();
        return true;
    }

    // Undo: Cmd+Z / Ctrl+Z
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        appState.editManager.undoLastAction();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
    {
        appState.editManager.undoLastAction();
        return true;
    }

    // Redo: Cmd+Shift+Z / Ctrl+Shift+Z
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier, 0))
    {
        appState.editManager.redoLastAction();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier
                                  | juce::ModifierKeys::shiftModifier, 0))
    {
        appState.editManager.redoLastAction();
        return true;
    }

    return false;
}

void MainComponent::toggleMixer()
{
    mixerVisible = !mixerVisible;
    mixerView.setVisible (mixerVisible);
    resized();   // Re-layout so ArrangementView expands/collapses accordingly
}
