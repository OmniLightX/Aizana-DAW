#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

struct ApplicationState;
class MixerEngine;

/**
 * BeginnerView — the Einsteiger-Welt (Phase 2, Block A).
 *
 * A second, calm UI surface rendering over the *same* te::Edit as the Pro view
 * (no logic duplication). The Dial (UIStateManager) decides which view is visible;
 * MainComponent swaps them losslessly (UI-04 by construction).
 *
 * Slice 1 = shell + working Dial.
 * Slice 2 = grouped lanes: the song shown as friendly named lanes built live from
 *           the real Edit. Named Bus Channels (MixerEngine) become groups that
 *           expand inline to their routed tracks (e.g. Drums -> Kick/Snare/HiHat);
 *           plain tracks are atomic lanes. This is the same grouping that Phase 3's
 *           stem-import will populate automatically.
 *
 * Live sync: rebuilt when shown (visibilityChanged) and via a slow Timer that
 * detects track/bus changes by signature — the heavy DAW editing happens in Pro,
 * so a 1 Hz safety poll while visible is ample.
 */
class BeginnerView : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    BeginnerView (ApplicationState& appState, MixerEngine& mixerEngine);
    ~BeginnerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void syncFromState();          // reflect UIStateManager into the Dial controls
    void rebuildModel();           // build the lane model from the live Edit
    juce::String modelSignature() const;
    void layoutLanes();            // compute per-lane rects (shared by paint + mouseDown)

    struct LaneVM
    {
        juce::String              name;
        juce::Colour              tint;
        bool                      isGroup  = false;
        bool                      expanded = false;
        std::vector<juce::String> children;
        juce::Rectangle<int>      rowBounds;   // header row (for hit-testing)
    };

    ApplicationState&    appState;
    MixerEngine&         mixerEngine;
    std::vector<LaneVM>  lanes;
    juce::String         lastSignature;

    // The Dial — mirrors the real UIStateManager (UIMode + assist prominence).
    juce::TextButton beginnerSeg { "\xe2\x97\x97 Einfach" };  // ◗ Einfach
    juce::TextButton proSeg      { "Pro \xe2\x97\x96" };       // Pro ◖
    juce::TextButton assistToggle { "Assist" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeginnerView)
};
