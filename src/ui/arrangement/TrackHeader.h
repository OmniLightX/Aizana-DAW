#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
#include "engine/AutomationEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"

namespace te = tracktion::engine;

/**
 * TrackHeader — Left-side header for a single audio track row in ArrangementView.
 *
 * Fixed width ~160px. Contains:
 *   - Track name label (editable on double-click)
 *   - Mute button (M)
 *   - Solo button (S)
 *   - Record arm button (R)
 *
 * Right-click shows a context menu with:
 *   - Add Plugin...       — opens PluginBrowserPopup, inserts into chain + opens editor
 *   - Create Bus from Track
 *   - Set Sidechain Source...
 *   - Rename Track
 *   - Delete Track
 *
 * Calls MixerEngine for mute/solo so they are undoable.
 */
class TrackHeader : public juce::Component
{
public:
    TrackHeader (te::AudioTrack&       track,
                 MixerEngine&          mixerEngine,
                 AutomationEngine&     automationEngine,
                 PluginManager&        pluginManager,
                 PluginWindowManager&  pluginWindowManager,
                 PluginScanner&        pluginScanner);

    void resized()  override;
    void paint     (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    te::AudioTrack& getTrack() const noexcept { return track; }

    // Selection state
    bool isSelected() const noexcept { return selected; }
    void setSelected (bool shouldBeSelected);

    // Callback: called by mouseDown (left click) to notify ArrangementView
    std::function<void (TrackHeader*)> onSelectionRequest;

private:
    te::AudioTrack&      track;
    MixerEngine&         mixerEngine;
    AutomationEngine&    automationEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;

    juce::Label      trackNameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::TextButton armButton  { "REC" };

    juce::TextButton readButton  { "R" };
    juce::TextButton writeButton { "W" };
    juce::TextButton touchButton { "T" };
    juce::TextButton latchButton { "L" };

    juce::Slider volumeSlider;
    juce::Label  panLabel;

    bool selected = false;

    void muteClicked();
    void soloClicked();
    void armClicked();

    void setAutomationMode (AutomationMode mode);
    void updateModeButtonStates();

    void updatePanLabel();

    // Context menu
    void showContextMenu();
    void handleAddPlugin();
    void handleInsertBuiltInFX (const juce::String& fxName);
    void handleCreateBus();
    void handleSetSidechainSource();
    void handleRenameTrack();
    void handleDeleteTrack();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};
