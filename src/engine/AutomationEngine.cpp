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

te::AutomatableParameter* AutomationEngine::resolveParameter (te::Track& track,
                                                              const juce::String& parameterID)
{
    // Qualifizierte ID "plug:<pluginItemID>:<paramID>" — eindeutig auch bei
    // mehreren Instanzen desselben Plugins auf der Spur (T1.1).
    if (parameterID.startsWith ("plug:"))
    {
        const auto rest     = parameterID.fromFirstOccurrenceOf ("plug:", false, false);
        const auto itemStr  = rest.upToFirstOccurrenceOf (":", false, false);
        const auto paramStr = rest.fromFirstOccurrenceOf (":", false, false);

        if (auto* at = dynamic_cast<te::AudioTrack*> (&track))
            for (auto p : at->pluginList)
                if (p != nullptr && p->itemID.toString() == itemStr)
                    for (auto* param : p->getAutomatableParameters())
                        if (param != nullptr && param->paramID == paramStr)
                            return param;

        return nullptr;
    }

    // Legacy-Kurznamen ("volume"/"pan"): erster Treffer über alle Track-Params —
    // trifft das VolumeAndPan-Plugin der Spur (bestehendes Verhalten).
    for (auto* param : track.getAllAutomatableParams())
        if (param->paramID == parameterID)
            return param;

    return nullptr;
}

te::AutomatableParameter* AutomationEngine::findParameter (te::Track& track,
                                                             const juce::String& parameterID) const
{
    return resolveParameter (track, parameterID);
}

//==============================================================================
// T1.1: Parameterliste für die UI + Normalisierung
//==============================================================================

juce::Array<AutomationEngine::ParamChoice> AutomationEngine::getAutomatableParams (te::Track& track) const
{
    juce::Array<ParamChoice> out;
    out.add ({ "volume", "Volume" });
    out.add ({ "pan",    "Pan" });

    auto* at = dynamic_cast<te::AudioTrack*> (&track);
    if (at == nullptr)
        return out;

    for (auto p : at->pluginList)
    {
        if (p == nullptr)
            continue;

        // Interne Struktur-Plugins raus; Sends BEHALTEN (Send-Level-Automation
        // ist Profi-Standard — „Reverb-Send im Chorus hochfahren").
        const auto type = p->getPluginType();
        if (type == "volume" || type == "level" || type == "auxreturn")
            continue;

        const auto prefix   = "plug:" + p->itemID.toString() + ":";
        const auto plugName = p->getName();

        for (auto* param : p->getAutomatableParameters())
            if (param != nullptr && param->isParameterActive())
                out.add ({ prefix + param->paramID,
                           plugName + juce::String::fromUTF8 (" \xc2\xb7 ") + param->getParameterName() });
    }

    return out;
}

float AutomationEngine::toNorm (const te::AutomatableParameter& p, float nativeValue) noexcept
{
    const auto& r = p.valueRange;   // NormalisableRange — convertTo0to1 respektiert Skew
    if (r.getRange().getLength() <= 0.0f)
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f,
                         r.convertTo0to1 (juce::jlimit (r.start, r.end, nativeValue)));
}

float AutomationEngine::fromNorm (const te::AutomatableParameter& p, float norm) noexcept
{
    const auto& r = p.valueRange;
    if (r.getRange().getLength() <= 0.0f)
        return r.start;
    return r.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm));
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
    int index = curve.getNearestPoint (time, dummyValue, 0.001);

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
