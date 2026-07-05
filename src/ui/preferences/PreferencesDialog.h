#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"

namespace te = tracktion::engine;

/**
 * PreferencesDialog — Application preferences with MIDI Clock section.
 *
 * Phase 1 content:
 *   - MIDI Clock: master/slave selector, BPM override
 *
 * Opened via Cmd+, (macOS) / Ctrl+, (Windows) in MainComponent.
 *
 * CONTEXT.md: "Aizana can act as MIDI clock master or slave."
 */
class PreferencesDialog : public juce::Component
{
public:
    explicit PreferencesDialog (ApplicationState& appState);
    ~PreferencesDialog() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Open as a modal dialog owned by the given parent component. */
    static void show (ApplicationState& appState);

private:
    ApplicationState& appState;

    // ---- MIDI Clock section ----
    juce::Label      midiClockSectionLabel  { {}, "MIDI Clock" };
    juce::ComboBox   midiClockModeCombo;                  // "Off", "Master", "Slave"
    juce::Label      midiClockModeLabel     { {}, "Mode:" };
    juce::Label      midiClockNoteLabel     { {}, "As master, Aizana broadcasts MIDI clock on all active outputs.\n"
                                                  "As slave, Aizana syncs its transport to incoming MIDI clock." };

    void midiClockModeChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesDialog)
};
