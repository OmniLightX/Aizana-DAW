#include <catch2/catch_test_macros.hpp>
#include "engine/EditManager.h"
#include "ui/automation/AutomationLane.h"

TEST_CASE("Automation lane follows external modes and first dropdown selection", "[automation-ui]")
{
    EditManager em;
    em.createNewEdit();
    auto* track = te::getAudioTracks (*em.getEdit()).getFirst();
    REQUIRE (track != nullptr);
    AutomationEngine engine (em);
    engine.setMode (*track, AutomationMode::read);
    double zoom = 100.0, scroll = 0.0;
    AutomationLane lane (*track, "volume", engine, zoom, scroll);
    juce::ComboBox* mode = nullptr;
    juce::ComboBox* parameter = nullptr;
    for (auto* child : lane.getChildren())
        if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
        {
            if (combo->getTitle().startsWith ("Automations-Modus")) mode = combo;
            else if (combo->getTitle().startsWith ("Automations-Parameter")) parameter = combo;
        }
    REQUIRE (mode != nullptr);
    REQUIRE (parameter != nullptr);
    const AutomationMode modes[] { AutomationMode::read, AutomationMode::touch,
                                  AutomationMode::latch, AutomationMode::write };
    for (int i = 0; i < 4; ++i)
    {
        engine.setMode (*track, modes[i]);
        CHECK (mode->getSelectedId() == i + 1);
        CHECK (engine.getMode (*track) == modes[i]);
    }
    engine.addAutomationPoint (*track, "volume", 1.0, -6.0f);
    const auto curveBefore = track->getVolumePlugin()->volParam->getCurve().state.createCopy();
    engine.setMode (*track, AutomationMode::read);
    mode->setSelectedId (2, juce::sendNotificationSync);
    CHECK (engine.getMode (*track) == AutomationMode::touch);
    CHECK (mode->getSelectedId() == 2);
    lane.setParameterID ("pan");
    CHECK (lane.getParameterID() == "pan");
    CHECK (parameter->getSelectedId() == 2);
    CHECK (engine.getMode (*track) == AutomationMode::touch);
    CHECK (track->getVolumePlugin()->volParam->getCurve().state.isEquivalentTo (curveBefore));
    auto& undo = em.getEdit()->getUndoManager();
    undo.beginNewTransaction ("Mode");
    engine.setMode (*track, AutomationMode::write);
    undo.beginNewTransaction();
    REQUIRE (undo.undo());
    CHECK (mode->getSelectedId() == 2);
    REQUIRE (undo.redo());
    CHECK (mode->getSelectedId() == 4);
}
