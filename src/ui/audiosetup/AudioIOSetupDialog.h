#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/InputBusManager.h"

namespace te = tracktion::engine;

/**
 * AudioIOSetupDialog — Named Input Bus manager (Phase 2, Studio One I/O Setup).
 *
 * Edits the session's Named Input Buses: the semantic layer between hardware
 * channels and track inputs. Each bus maps a stable name ("Vocal Mic") to a
 * hardware input channel; tracks then select buses, never raw hardware.
 *
 * Per bus row: editable name · hardware-channel picker · delete.
 * Plus "+ Add Bus" to create one, mapped to a hardware input.
 *
 * All mutations route through InputBusManager (undoable) and re-apply the
 * names onto the WaveInputDevice aliases (applyToEngine).
 *
 * Accessible via: Song Setup ▸ Audio I/O…  and the track input dropdown's
 * "Manage Buses…" entry.
 */
class AudioIOSetupDialog : public juce::Component
{
public:
    AudioIOSetupDialog (te::Engine& engine, InputBusManager& busManager);

    void resized() override;
    void paint   (juce::Graphics& g) override;

    /** Open the dialog as an async window. */
    static void show (te::Engine& engine, InputBusManager& busManager);

private:
    te::Engine&      engine;
    InputBusManager& busManager;

    juce::Label      titleLabel;
    juce::Label      hintLabel;
    juce::TextButton addBusButton { "+ Add Bus" };
    juce::TextButton closeButton  { "Close" };

    // Hardware input options (display name + first hardware channel index),
    // built once from the device manager and shared by every row's channel combo.
    struct HwInput { juce::String name; int channelIndex = -1; };
    juce::Array<HwInput> hwInputs;
    void buildHwInputList();
    int  comboIndexForChannel (int channelIndex) const;

    // One editable row per Named Bus.
    struct BusRow : public juce::Component
    {
        juce::Label        nameEditor;
        juce::ComboBox     channelCombo;
        juce::ToggleButton stereoToggle;
        juce::TextButton   deleteButton;

        std::function<void (const juce::String&)> onRename;
        std::function<void (int /*comboItemIndex*/)> onRemap;
        std::function<void (bool /*stereo*/)>     onSetStereo;
        std::function<void ()>                    onDelete;

        BusRow();
        void resized() override;
    };

    juce::Viewport            busViewport;
    juce::Component           busListContainer;
    juce::OwnedArray<BusRow>  rows;

    void rebuildBuses();
    /** Rebuild the bus rows on the next message tick. Structural changes (delete /
     *  remap / stereo) are triggered from a row's own callback, so destroying the
     *  rows synchronously would free the component mid-callback — defer instead. */
    void scheduleRebuild();

    static constexpr int kRowHeight = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioIOSetupDialog)
};
