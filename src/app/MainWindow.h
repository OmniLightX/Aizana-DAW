#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ApplicationState.h"

//==============================================================================
/**
 * MainWindow — the top-level document window for the Aizana DAW.
 *
 * Phase 1 skeleton: displays a blank gray content component. Full UI
 * (arrangement view, mixer, transport bar) is built in plans 01-03 through
 * 01-10.
 *
 * Window model (from CONTEXT.md):
 *   - Single main window with dockable/resizable regions
 *   - Fixed transport bar at top
 *   - Arrangement view (tracks + timeline) in upper region
 *   - Mixer view (channel strips) in lower region (visible by default)
 *
 * ApplicationState is constructed here to ensure the audio device and Edit
 * are initialised before any UI is shown (plan 01-02 requirement).
 *
 * Plan 01-13: On macOS, installs MainComponent as the native system menu bar
 * via juce::MenuBarModel::setMacMainMenu(). On Windows/Linux, adds a
 * juce::MenuBarComponent at the top of the window.
 */
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (juce::String name);
    ~MainWindow() override;

    void closeButtonPressed() override;
    void resized() override;

    /** Access the application state (EditManager + AudioEngine) from any UI component. */
    ApplicationState& getApplicationState() { return *appState; }

    /** Session-Datei oeffnen (Programmstart mit Argument, Doppelklick im Finder).
        Geht durch dieselbe Tuer wie "Open Session…" — nur ohne Dateidialog. */
    bool openSession (const juce::File& file);

private:
    std::unique_ptr<ApplicationState> appState;

    // Only used on non-macOS platforms (Windows / Linux):
    std::unique_ptr<juce::MenuBarComponent> menuBarComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
