#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

struct ApplicationState;

/**
 * BeginnerView — the Einsteiger-Welt (Phase 2, Block A, Slice 1: the shell).
 *
 * A second, calm UI surface that renders over the *same* te::Edit as the Pro view
 * (no logic duplication — same engines, same session). The Dial (UIStateManager)
 * decides which view is visible; MainComponent swaps them. Switching is lossless
 * because both are just render layers over one Edit (UI-04 by construction).
 *
 * Slice 1 = the shell + a working Dial (mode segmented + assist toggle) so the two
 * views can be switched both ways. Grouped lanes (Slice 2), the dockable Coach rail
 * (Slice 3), and intent + "The Reveal" (Slice 4) land on top of this.
 *
 * Listens to UIStateManager so its Dial controls stay in sync when the mode/assist
 * is changed elsewhere (e.g. the View menu or — later — the TransportBar dial).
 */
class BeginnerView : public juce::Component,
                     private juce::ChangeListener
{
public:
    explicit BeginnerView (ApplicationState& appState);
    ~BeginnerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void syncFromState();   // reflect UIStateManager into the Dial controls

    ApplicationState& appState;

    // The Dial — mirrors the real UIStateManager (UIMode + assist prominence).
    juce::TextButton beginnerSeg { "\xe2\x97\x97 Einfach" };  // ◗ Einfach
    juce::TextButton proSeg      { "Pro \xe2\x97\x96" };       // Pro ◖
    juce::TextButton assistToggle { "Assist" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeginnerView)
};
