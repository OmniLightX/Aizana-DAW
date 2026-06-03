#include "UIStateManager.h"

#include <tracktion_engine/tracktion_engine.h>

UIStateManager::UIStateManager (te::Engine& e)
    : engine (e)
{
    loadFromStorage();
}

void UIStateManager::setMode (UIMode newMode)
{
    if (mode == newMode)
        return;                                    // no-op: never resets an in-mode override

    mode          = newMode;
    assistEnabled = defaultAssistFor (newMode);    // new mode → its default prominence
    persist();
    sendChangeMessage();
}

void UIStateManager::toggleMode()
{
    setMode (mode == UIMode::Beginner ? UIMode::Pro : UIMode::Beginner);
}

void UIStateManager::setAssistEnabled (bool shouldBeEnabled)
{
    if (assistEnabled == shouldBeEnabled)
        return;

    assistEnabled = shouldBeEnabled;
    persist();
    sendChangeMessage();
}

void UIStateManager::toggleAssist()
{
    setAssistEnabled (! assistEnabled);
}

void UIStateManager::setFocusedTrackIndex (int trackIndex)
{
    if (workContext.focusedTrackIndex == trackIndex)
        return;

    workContext.focusedTrackIndex = trackIndex;
    sendChangeMessage();                           // runtime context — intentionally not persisted
}

void UIStateManager::setOnboardingCompleted (bool completed)
{
    if (onboardingCompleted == completed)
        return;
    onboardingCompleted = completed;
    auto& props = engine.getPropertyStorage().getPropertiesFile();
    props.setValue (kOnboardingKey, completed);
    props.saveIfNeeded();
    sendChangeMessage();
}

void UIStateManager::persist()
{
    auto& props = engine.getPropertyStorage().getPropertiesFile();
    props.setValue (kModeKey, mode == UIMode::Pro ? "pro" : "beginner");
    props.setValue (kAssistKey, assistEnabled);
    props.saveIfNeeded();
}

void UIStateManager::loadFromStorage()
{
    auto& props = engine.getPropertyStorage().getPropertiesFile();

    const auto modeStr = props.getValue (kModeKey, "beginner");
    mode = (modeStr == "pro") ? UIMode::Pro : UIMode::Beginner;

    // If assist was never persisted, fall back to the current mode's default.
    assistEnabled = props.getBoolValue (kAssistKey, defaultAssistFor (mode));

    onboardingCompleted = props.getBoolValue (kOnboardingKey, false);
}
