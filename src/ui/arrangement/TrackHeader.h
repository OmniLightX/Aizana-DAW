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

    // Sync button visual state from actual track mute/solo state
    void refresh();

    /** Toggle mute via keyboard shortcut — sets button state directly then calls MixerEngine.
     *  Mirrors toggleArm(): direct visual update, no TE read-back dependency. */
    void toggleMute();

    /** Toggle solo via keyboard shortcut — sets button state directly then calls MixerEngine.
     *  Mirrors toggleArm(): direct visual update, no TE read-back dependency. */
    void toggleSolo();

    /** Toggle record arm via keyboard shortcut — sets button state then calls armClicked().
     *  Equivalent to physically pressing the REC button.
     *  Safe to call from ArrangementView::keyPressed(). */
    void toggleArm();

    /** Rebuild the input selector from live DeviceManager state.
     *  Call after audio device changes or on construction. */
    void populateInputSelector();

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
    juce::TextButton muteButton    { "M"   };
    juce::TextButton soloButton    { "S"   };
    juce::TextButton armButton     { "REC" };
    juce::TextButton monitorButton { "I"   };  ///< Input monitoring toggle

    /** Input selector — shows assigned hardware input, click to change.
     *  Studio One style: small label-like button below M/S/REC/I row.
     *  Text = device name (truncated) or "No Input". */
    juce::TextButton inputButton;

    juce::Slider volumeSlider;
    juce::Label  panLabel;

    bool selected = false;

    void muteClicked();
    void soloClicked();
    void armClicked();
    void monitorClicked();

    /** Show popup menu listing all enabled WaveInputDevices + "No Input". */
    void showInputMenu();
    /** Assign waveIn to this track (nullptr = no input). */
    void assignInput (te::WaveInputDevice* waveIn);
    /** Sync inputButton text from the currently assigned input device. */
    void refreshInputButton();

    void updatePanLabel();

    // Context menu
    void showContextMenu();
    void handleAddPlugin();
    void handleInsertBuiltInFX (const juce::String& fxName);
    void handleCreateBus();
    void handleCreateFXChannel();
    void handleSetSidechainSource();
    void handleRenameTrack();
    void handleDeleteTrack();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};
