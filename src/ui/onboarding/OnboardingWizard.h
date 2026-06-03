#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

struct ApplicationState;

/**
 * OnboardingWizard — first-run experience (Phase 2, Block C).
 *
 * A calm, jargon-free, full-window overlay shown once on first launch (gated by
 * UIStateManager::isOnboardingCompleted). Steps: Welcome -> Genre -> Confirm. On
 * finish it builds a READY starter project (named tracks + tempo for the chosen
 * genre — never an empty session), drops the user into the Beginner world with
 * assist on, and marks onboarding complete (persisted).
 *
 * The genre also seeds a small Phase-2 payoff (a loudness target shown on Confirm);
 * Phase 3 uses it for real reference matching.
 */
class OnboardingWizard : public juce::Component
{
public:
    OnboardingWizard (ApplicationState& appState, std::function<void()> onFinished);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override {}   // swallow backdrop clicks (modal)

private:
    void gotoStep (int newStep);
    void finish();
    void buildStarterProject();

    ApplicationState&     appState;
    std::function<void()> onFinished;

    int step       = 0;   // 0 Welcome, 1 Genre, 2 Confirm
    int genreIndex = 0;

    juce::OwnedArray<juce::TextButton> genreButtons;
    juce::TextButton backButton { "Zurueck" };
    juce::TextButton nextButton { "Los geht's" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnboardingWizard)
};
