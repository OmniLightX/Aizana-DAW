#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "engine/MixerEngine.h"

namespace te = tracktion::engine;

/**
 * InspectorPanel — left dock with parameters of the current selection
 * (Studio One inspector, scoped to Cloudes essentials).
 *
 *   [ TRACK ──────────────── ]   shown when a track is selected
 *   [  Name      (editable)  ]
 *   [  Volume    slider dB   ]
 *   [  Pan       slider      ]
 *   [ EVENT ──────────────── ]   shown when a clip is selected
 *   [  Name      (editable)  ]
 *   [  Start / Länge   (s)   ]
 *   [  Fade In / Out   (s)   ]
 *   [  Clip-Gain      (dB)   ]
 *
 * Data flow: PULL — a 250 ms timer polls the selection providers and refreshes
 * idle controls (editing controls are never overwritten mid-edit). All writes
 * go through the Edit's UndoManager, one transaction per gesture, using the
 * same TE pathways as the Mixer/ClipComponent (MixerEngine, te::IDs::fadeIn…).
 */
class InspectorPanel : public juce::Component,
                       private juce::Timer
{
public:
    InspectorPanel (EditManager&, MixerEngine&);
    ~InspectorPanel() override;

    /** Selection providers — wired by MainComponent to the ArrangementView. */
    std::function<te::AudioTrack*()> trackProvider;
    std::function<te::Clip*()>       clipProvider;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    static constexpr int kWidth = 240;

private:
    EditManager& editManager;
    MixerEngine& mixerEngine;

    // ---- Track section ----
    juce::Label trackHeading, trackNameLabel;
    juce::Slider volumeSlider, panSlider;
    juce::Label volumeCaption, panCaption;

    // ---- Event/clip section ----
    juce::Label eventHeading, clipNameLabel;
    juce::Label startCaption, lengthCaption, fadeInCaption, fadeOutCaption, gainCaption;
    juce::Label startValue, lengthValue, fadeInValue, fadeOutValue;
    juce::Slider gainSlider;

    juce::Label emptyHint;

    // Currently shown objects (identity check for refresh)
    te::AudioTrack* shownTrack = nullptr;
    te::Clip*       shownClip  = nullptr;

    void timerCallback() override;
    void refreshFromSelection();
    void refreshTrackValues();
    void refreshClipValues();

    // Write helpers (message thread, one undo transaction each)
    void applyTrackName();
    void applyClipName();
    void applyStart();
    void applyLength();
    void applyFade (bool fadeIn);

    static juce::String secondsText (double secs) { return juce::String (secs, 3) + " s"; }
    static double parseSeconds (const juce::String& text) { return text.getDoubleValue(); }

    void styleCaption (juce::Label&, const juce::String& text);
    void styleValue (juce::Label&, std::function<void()> onCommit);
    void styleHeading (juce::Label&, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};
