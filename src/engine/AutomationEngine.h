#pragma once

#include "EditManager.h"

// Use the AutomationMode enum defined in Tracktion Engine.
// tracktion::engine::AutomationMode has read, touch, latch, write variants.
using AutomationMode = tracktion::engine::AutomationMode;

/**
 * AutomationEngine — Read/write/touch/latch automation on any ValueTree parameter.
 *
 * Covers requirements:
 *   MIX-04: Draw and edit automation for any AutomatableParameter on a track.
 *
 * Design invariants:
 *   - Automation curve mutations route through editManager.getUndoManager()
 *   - Each addAutomationPoint() call begins a new transaction (individually undoable)
 *   - clearAutomation() uses a single transaction for the entire clear operation
 *   - AutomationMode is a CachedValue on the Track, stored in the Edit ValueTree
 *
 * Usage:
 *   AutomationEngine ae(editManager);
 *   ae.addAutomationPoint(*track, "volume", 2.0, -12.0f);
 *   float v = ae.getValueAtTime(*track, "volume", 1.0);  // interpolated
 *   ae.setMode(*track, AutomationMode::write);
 */
class AutomationEngine
{
public:
    explicit AutomationEngine (EditManager& editManager);

    // -------------------------------------------------------------------------
    // T1.1: Plugin-Parameter-Automation — beliebige AutomatableParameter
    // -------------------------------------------------------------------------

    /**
     * Ein automatisierbarer Parameter der Spur für die UI-Auswahl.
     *
     * `id` ist entweder ein Legacy-Kurzname ("volume"/"pan" — VolumeAndPan-Plugin)
     * oder eine QUALIFIZIERTE ID `plug:<pluginItemID>:<paramID>`. Die Qualifizierung
     * ist nötig, weil paramIDs über mehrere Instanzen desselben Plugins NICHT
     * eindeutig sind (2× "Aizana EQ" auf einer Spur = identische paramIDs).
     */
    struct ParamChoice
    {
        juce::String id;            // "volume" | "pan" | "plug:<itemID>:<paramID>"
        juce::String displayName;   // "Volume" | "Aizana EQ · Band 1 Gain" | …
    };

    /**
     * Alle automatisierbaren Parameter der Spur in UI-Reihenfolge:
     * Volume, Pan, dann je User-Plugin (inkl. Sends, ohne interne volume/level/
     * auxreturn-Plugins) alle Parameter als qualifizierte IDs.
     */
    juce::Array<ParamChoice> getAutomatableParams (te::Track& track) const;

    /** Normalisierung über die valueRange des Parameters (0..1 ↔ nativ).
        Fixt nebenbei Pan: dessen Range ist -1..1, nicht 0..1. */
    static float toNorm   (const te::AutomatableParameter& p, float nativeValue) noexcept;
    static float fromNorm (const te::AutomatableParameter& p, float norm) noexcept;

    /** Öffentlicher Resolver (auch von AutomationLane genutzt): versteht
        Legacy-Kurznamen UND qualifizierte `plug:`-IDs. nullptr wenn nicht gefunden. */
    static te::AutomatableParameter* resolveParameter (te::Track& track,
                                                       const juce::String& parameterID);

    // -------------------------------------------------------------------------
    // MIX-04: Draw and edit automation for any parameter
    // -------------------------------------------------------------------------

    /**
     * Add an automation point at the given time and value.
     *
     * Parameter IDs for standard plugins:
     *   VolumeAndPanPlugin: "volume" (slider position, not dB), "pan"
     *   Use plugin->volParam->paramID for the actual ID.
     *
     * Time is in seconds from the edit start.
     * Value is in the parameter's native units.
     */
    void addAutomationPoint (te::Track& track,
                             const juce::String& parameterID,
                             double timeSeconds,
                             float value);

    /**
     * Remove the automation point closest to the given time.
     * No-op if no points exist on the curve.
     */
    void removeAutomationPoint (te::Track& track,
                                const juce::String& parameterID,
                                double timeSeconds);

    /**
     * Get the interpolated value at the given time.
     * Returns the parameter's default value if no automation points exist.
     */
    float getValueAtTime (te::Track& track,
                          const juce::String& parameterID,
                          double timeSeconds) const;

    /**
     * Remove all automation points for the given parameter on this track.
     * Records as a single undo step.
     */
    void clearAutomation (te::Track& track, const juce::String& parameterID);

    // -------------------------------------------------------------------------
    // Mode control (Read/Write/Touch/Latch)
    // -------------------------------------------------------------------------

    /**
     * Set the automation mode for all parameters on this track.
     * Mode is stored in the Track's CachedValue<AutomationMode> automationMode.
     */
    void setMode (te::Track& track, AutomationMode mode);

    /**
     * Return the current automation mode for this track.
     */
    AutomationMode getMode (const te::Track& track) const;

private:
    EditManager& editManager;

    /**
     * Find the AutomatableParameter on the track's plugins with the given paramID.
     * Delegates to resolveParameter (legacy short names + qualified plug:-IDs).
     */
    te::AutomatableParameter* findParameter (te::Track& track,
                                              const juce::String& parameterID) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEngine)
};
