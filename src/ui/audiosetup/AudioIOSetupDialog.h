#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * AudioIOSetupDialog — Studio One-style Audio I/O configuration dialog.
 *
 * Shows all hardware input devices available from the active audio driver.
 * Users can see the canonical name of each input so they know what to
 * select in the per-track input dropdown (TrackHeader::inputButton).
 *
 * Accessible via: Song Setup > Audio I/O...
 *
 * Layout:
 *   - "Inputs" section header
 *   - One row per WaveInputDevice: index, name, channel count, enabled indicator
 *   - "Close" button at bottom
 *
 * Future: support renaming inputs and creating named stereo pairs (v2).
 */
class AudioIOSetupDialog : public juce::Component
{
public:
    explicit AudioIOSetupDialog (te::Engine& engine);

    void resized() override;
    void paint   (juce::Graphics& g) override;

    /** Open the dialog as a modal window. */
    static void show (te::Engine& engine);

private:
    te::Engine& engine;

    juce::Label       titleLabel;
    juce::TextButton  closeButton { "Close" };

    // One row per hardware input device
    struct InputRow : public juce::Component
    {
        juce::Label indexLabel;
        juce::Label nameLabel;
        juce::Label channelLabel;
        juce::Label statusLabel;

        void init (int idx, const juce::String& name, int numChannels, bool enabled);
        void resized() override;
    };

    juce::OwnedArray<InputRow> rows;

    void populate();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioIOSetupDialog)
};
