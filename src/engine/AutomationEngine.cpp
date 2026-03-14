#include "AutomationEngine.h"

// Convenience alias for Tracktion's strong time type
using TimePos = tracktion::core::TimePosition;

//==============================================================================
AutomationEngine::AutomationEngine (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
// Parameter lookup helper
//==============================================================================

te::AutomatableParameter* AutomationEngine::findParameter (te::Track& track,
                                                             const juce::String& parameterID) const
{
    // Walk all AutomatableParameters on the track's plugins and find the one
    // whose paramID matches the requested ID.
    for (auto* param : track.getAllAutomatableParams())
    {
        if (param->paramID == parameterID)
            return param;
    }

    return nullptr;
}

//==============================================================================
// MIX-04: Automation point add/remove/get/clear
//==============================================================================

void AutomationEngine::addAutomationPoint (te::Track& track,
                                            const juce::String& parameterID,
                                            double timeSeconds,
                                            float value)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* param = findParameter (track, parameterID);
    if (param == nullptr)
        return;

    auto& curve = param->getCurve();

    // Each point addition is its own undo step — individually undoable per MIX-04
    edit->getUndoManager().beginNewTransaction ("Add automation point");

    // addPoint(time, value, curve_shape) where curve=0.0 = linear
    curve.addPoint (TimePos::fromSeconds (timeSeconds), value, 0.0f);
}

void AutomationEngine::removeAutomationPoint (te::Track& track,
                                               const juce::String& parameterID,
                                               double timeSeconds)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* param = findParameter (track, parameterID);
    if (param == nullptr)
        return;

    auto& curve = param->getCurve();
    if (curve.getNumPoints() == 0)
        return;

    // Find the index of the point closest to the given time
    TimePos time = TimePos::fromSeconds (timeSeconds);
    float dummyValue = 0.0f;
    int index = curve.getNearestPoint (time, dummyValue, 1.0);

    if (index >= 0)
    {
        edit->getUndoManager().beginNewTransaction ("Remove automation point");
        curve.removePoint (index);
    }
}

float AutomationEngine::getValueAtTime (te::Track& track,
                                         const juce::String& parameterID,
                                         double timeSeconds) const
{
    auto* param = findParameter (const_cast<te::Track&> (track), parameterID);
    if (param == nullptr)
        return 0.0f;

    auto& curve = param->getCurve();
    if (curve.getNumPoints() == 0)
        return param->getCurrentValue();

    return curve.getValueAt (TimePos::fromSeconds (timeSeconds));
}

void AutomationEngine::clearAutomation (te::Track& track, const juce::String& parameterID)
{
    auto* edit = editManager.getEdit();
    jassert (edit != nullptr);

    auto* param = findParameter (track, parameterID);
    if (param == nullptr)
        return;

    edit->getUndoManager().beginNewTransaction ("Clear automation");
    param->getCurve().clear();
}

//==============================================================================
// Mode control (Read/Write/Touch/Latch)
//==============================================================================

void AutomationEngine::setMode (te::Track& track, AutomationMode mode)
{
    // AutomationMode is stored as a CachedValue on the Track.
    // It controls how automation is read/written during playback.
    // Setting it here persists to the Edit ValueTree automatically.
    track.automationMode = mode;
}

AutomationMode AutomationEngine::getMode (const te::Track& track) const
{
    return track.automationMode;
}
