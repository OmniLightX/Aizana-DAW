#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/MixerEngine.h"
#include "engine/InputBusManager.h"
#include "engine/AutomationEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "ui/common/TrackColours.h"

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
                 PluginScanner&        pluginScanner,
                 InputBusManager&      inputBusManager);

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

    /** Called after the track colour changes so ArrangementView can repaint the
     *  row wash + clips (which read the same colour via CloudesTrackColours). */
    std::function<void ()> onColourChanged;

private:
    /** Dedicated clickable colour bar on the left edge (Studio One style): shows the
     *  track's display colour; a left-click opens the colour menu. A separate child
     *  component so the click target is unambiguous (not stolen by the name label). */
    struct ColourBar : public juce::Component,
                       public juce::SettableTooltipClient
    {
        te::Track&            track;
        std::function<void()> onClick;
        explicit ColourBar (te::Track& t) : track (t) { setTooltip (juce::String::fromUTF8 ("Track-Farbe w\xc3\xa4hlen (Klick)")); }
        void paint (juce::Graphics& g) override { g.fillAll (CloudesTrackColours::displayColour (track)); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() && onClick) onClick();
        }
    };

    te::AudioTrack&      track;
    ColourBar            colourBar { track };
    MixerEngine&         mixerEngine;
    AutomationEngine&    automationEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;
    InputBusManager&     inputBusManager;

    juce::Label      trackNameLabel;
    juce::TextButton muteButton    { "M"   };
    juce::TextButton soloButton    { "S"   };
    juce::TextButton armButton     { "REC" };
    juce::TextButton monitorButton { "I"   };  ///< Input monitoring toggle

    /** Input selector — shows assigned hardware input, click to change.
     *  Studio One style: small label-like button below M/S/REC/I row.
     *  Text = device name (truncated) or "No Input". */
    juce::TextButton inputButton;

    /** Mono/Stereo track-format toggle (Studio One style) — right of the input row.
     *  One click flips the track's channel format. It does NOT reconfigure hardware
     *  I/O (no channel is consumed), so it never takes a neighbouring input offline.
     *  Real 2-channel stereo input is a separate, explicit Bus-manager setting.
     *  Shows "M" or "St"; hidden for MIDI tracks. */
    juce::TextButton stereoButton;

    juce::Slider volumeSlider;
    juce::Label  panLabel;

    bool selected = false;

    void muteClicked();
    void soloClicked();
    void armClicked();
    void monitorClicked();

    /** Returns true if this track is a MIDI track (has at least one MidiClip). */
    bool isMidiTrack() const;

    /** Show popup menu: WaveInputDevices for audio tracks, MidiInputDevices for MIDI tracks. */
    void showInputMenu();
    /** Assign waveIn to this track (nullptr = no input). Audio tracks only. */
    void assignInput (te::WaveInputDevice* waveIn);
    /** Assign midiIn to this track (nullptr = no input). MIDI tracks only. */
    void assignMidiInput (te::MidiInputDevice* midiIn);
    /** Sync inputButton text from the currently assigned input device (audio or MIDI). */
    void refreshInputButton();
    /** Open the Named Input Bus manager (from the "Manage Buses…" dropdown entry). */
    void handleManageBuses();

    /** Flip the track's channel format between mono and stereo (Studio One one-click).
     *  Pure track-format property — does NOT touch hardware I/O, so no input channel
     *  is consumed. Real 2-channel stereo input is configured in the Bus manager. */
    void toggleTrackStereo();
    /** Sync the mono/stereo button text + state from the track's format property. */
    void refreshStereoButton();

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

    /** Fill a PopupMenu with palette swatches + Custom… + Standard (Auto). */
    void populateColourMenu (juce::PopupMenu& menu);
    /** Show the colour menu standalone (anchored to the colour bar) on bar-click. */
    void showTrackColourMenu();
    /** Set the track colour (undoable). Empty colour resets to the auto palette. */
    void handleSetTrackColour (juce::Colour newColour);
    /** Open a ColourSelector in a CallOutBox for a free custom track colour. */
    void handleCustomColourPicker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeader)
};
