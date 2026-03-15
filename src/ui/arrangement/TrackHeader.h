#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
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
                 PluginManager&        pluginManager,
                 PluginWindowManager&  pluginWindowManager,
                 PluginScanner&        pluginScanner);

    void resized() override;
    void paint   (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    te::AudioTrack& getTrack() const noexcept { return track; }

private:
    te::AudioTrack&      track;
    MixerEngine&         mixerEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;

    juce::Label      trackNameLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::TextButton armButton  { "R" };

    void muteClicked();
    void soloClicked();
    void armClicked();

    // Context menu
    void showContextMenu();
    void handleAddPlugin();
    void handleCreateBus();
    void handleSetSidechainSource();
    void handleRenameTrack();
    void handleDeleteTrack();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};
