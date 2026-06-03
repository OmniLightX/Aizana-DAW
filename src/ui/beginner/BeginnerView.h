#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include "ai/HeuristicFeedbackEngine.h"   // Feedback + IntentEngine::Intent

struct ApplicationState;
class MixerEngine;

/**
 * BeginnerView — the Einsteiger-Welt (Phase 2, Block A).
 *
 * A second, calm UI surface rendering over the *same* te::Edit as the Pro view
 * (no logic duplication). The Dial (UIStateManager) decides which view is visible;
 * MainComponent swaps them losslessly (UI-04 by construction).
 *
 *   Slice 1 — shell + working Dial.
 *   Slice 2 — grouped lanes built live from the Edit (Named Bus = group, routed
 *             tracks = children; plain tracks atomic; FX hidden).
 *   Slice 3 — the synthesis A+B: a dockable Coach rail (shown when assist is on)
 *             with proactive feedback cards + 1-click intents + a command bar, and
 *             "The Reveal" overlay (what Cloudes did + "Zeig's mir" jumps to Pro).
 *             The feedback/intents are static previews here; Block B wires the real
 *             HeuristicFeedbackEngine + IntentEngine behind the same surface.
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
    void syncFromState();          // reflect UIStateManager into the Dial + rail visibility
    void rebuildModel();           // build the lane model from the live Edit
    juce::String modelSignature() const;
    void layoutLanes();            // compute per-lane rects (shared by paint + mouseDown)

    bool assistOn() const;
    juce::Rectangle<int> railArea() const;        // the Coach rail column (empty when assist off)
    juce::Rectangle<int> railCardRect (int i) const;
    void showReveal (const juce::String& title, const juce::String& body);

    // Block B wiring: live feedback + executing intents through the bridge.
    void refreshFeedback();                              // poll HeuristicFeedbackEngine
    void runIntent (IntentEngine::Intent intent);        // plan -> bridge -> Reveal
    static bool matchKeyword (const juce::String& text, IntentEngine::Intent& out);

    // "The Reveal" overlay — its own child Component so it paints above the rail
    // chrome (z-order) and captures clicks. ok closes; showPro bridges to the Pro view.
    struct Reveal : public juce::Component
    {
        Reveal();
        void show (const juce::String& t, const juce::String& b);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        std::function<void()> onShowPro;
        juce::String title, body;
        juce::TextButton ok      { "Gut so" };
        juce::TextButton showPro { "Zeig's mir \xe2\x86\x92" };  // Zeig's mir →
    };

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

    // Live Coach feedback (from HeuristicFeedbackEngine), refreshed while visible.
    std::vector<HeuristicFeedbackEngine::Feedback> feedback;
    juce::String lastFeedbackSig;   // change-detect to avoid needless rebinds/repaints

    // The Dial — mirrors the real UIStateManager (UIMode + assist prominence).
    juce::TextButton beginnerSeg { "\xe2\x97\x97 Einfach" };  // ◗ Einfach
    juce::TextButton proSeg      { "Pro \xe2\x97\x96" };       // Pro ◖
    juce::TextButton assistToggle { "Assist" };

    // Coach rail (Slice 3) — proactive feedback + 1-click intents + command bar.
    juce::TextButton act1 { "Nach vorne holen" };
    juce::TextButton act2 { "Punchiger machen" };
    juce::TextButton act3 { "Lauter machen" };
    juce::TextEditor cmdInput;

    Reveal reveal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeginnerView)
};
