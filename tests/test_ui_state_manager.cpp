// test_ui_state_manager.cpp — Phase 2, Block A: "Das Dial" (UIStateManager)
//
// Verifies the dial's contract (headless, deterministic):
//   - defaults: Beginner mode, assist on
//   - "ein Dial, zwei Defaults": switching mode applies that mode's default assist
//   - assist is an independent in-mode override; setMode(sameMode) is a no-op
//   - mode + assist round-trip through engine PropertyStorage (survives reconstruction)
//   - WorkContext.focusedTrackIndex updates, but is NOT persisted
//
// Note: the ChangeBroadcaster *delivery* path (sendChangeMessage → listeners) is
// async + coalesced and JUCE-owned; it is exercised for real when the UI is wired
// (live message loop), not asserted here. These tests cover the stateful contract.

#include <catch2/catch_test_macros.hpp>

#include "engine/EditManager.h"
#include "app/UIStateManager.h"

namespace
{
    // Must mirror UIStateManager's private PropertiesFile keys.
    constexpr const char* kModeKey   = "aizana_ui_mode";
    constexpr const char* kAssistKey = "aizana_assist_enabled";

    // The PropertiesFile is process/user-global (app "AizanaEngineTests"), so clear
    // the keys before tests that assert on un-persisted defaults — otherwise a prior
    // test run's persisted values would leak in and make this order-dependent.
    void clearPersistedDial (te::Engine& engine)
    {
        auto& props = engine.getPropertyStorage().getPropertiesFile();
        props.removeValue (kModeKey);
        props.removeValue (kAssistKey);
        props.saveIfNeeded();
    }
}

// ---------------------------------------------------------------------------
TEST_CASE ("UIStateManager: defaults to Beginner with assist on", "[ui][phase2][dial]")
{
    EditManager em;
    clearPersistedDial (em.getEngine());

    UIStateManager dial (em.getEngine());

    CHECK (dial.getMode() == UIStateManager::UIMode::Beginner);
    CHECK (dial.isAssistEnabled());
    CHECK (dial.getWorkContext().focusedTrackIndex == -1);
}

// ---------------------------------------------------------------------------
TEST_CASE ("UIStateManager: switching mode applies that mode's default assist",
           "[ui][phase2][dial]")
{
    EditManager em;
    clearPersistedDial (em.getEngine());
    UIStateManager dial (em.getEngine());

    dial.setMode (UIStateManager::UIMode::Pro);
    CHECK (dial.getMode() == UIStateManager::UIMode::Pro);
    CHECK_FALSE (dial.isAssistEnabled());           // Pro default = assist off

    dial.setMode (UIStateManager::UIMode::Beginner);
    CHECK (dial.getMode() == UIStateManager::UIMode::Beginner);
    CHECK (dial.isAssistEnabled());                 // Beginner default = assist on
}

// ---------------------------------------------------------------------------
TEST_CASE ("UIStateManager: assist is an independent in-mode override",
           "[ui][phase2][dial]")
{
    EditManager em;
    clearPersistedDial (em.getEngine());
    UIStateManager dial (em.getEngine());

    // Override OFF while in Beginner (default would be on).
    dial.setAssistEnabled (false);
    CHECK (dial.getMode() == UIStateManager::UIMode::Beginner);
    CHECK_FALSE (dial.isAssistEnabled());

    // setMode with the SAME mode is a no-op — it must NOT reset the override.
    dial.setMode (UIStateManager::UIMode::Beginner);
    CHECK_FALSE (dial.isAssistEnabled());

    // Override ON while in Pro (default would be off).
    dial.setMode (UIStateManager::UIMode::Pro);     // resets to Pro default (off)
    CHECK_FALSE (dial.isAssistEnabled());
    dial.setAssistEnabled (true);
    CHECK (dial.isAssistEnabled());
}

// ---------------------------------------------------------------------------
TEST_CASE ("UIStateManager: mode + assist round-trip through PropertyStorage",
           "[ui][phase2][dial][persist]")
{
    EditManager em;
    clearPersistedDial (em.getEngine());

    {
        UIStateManager dialA (em.getEngine());
        dialA.setMode (UIStateManager::UIMode::Pro);  // persists pro + assist off
        dialA.setAssistEnabled (true);                // persists assist on (override)
    }

    // A fresh dial on the same engine must read the persisted state back.
    UIStateManager dialB (em.getEngine());
    CHECK (dialB.getMode() == UIStateManager::UIMode::Pro);
    CHECK (dialB.isAssistEnabled());

    clearPersistedDial (em.getEngine());              // leave storage clean for other tests
}

// ---------------------------------------------------------------------------
TEST_CASE ("UIStateManager: WorkContext updates, broadcasts, is not persisted",
           "[ui][phase2][dial]")
{
    EditManager em;
    clearPersistedDial (em.getEngine());

    {
        UIStateManager dialA (em.getEngine());
        dialA.setFocusedTrackIndex (3);
        CHECK (dialA.getWorkContext().focusedTrackIndex == 3);
    }

    // Work context is runtime-only — a fresh dial starts at -1.
    UIStateManager dialB (em.getEngine());
    CHECK (dialB.getWorkContext().focusedTrackIndex == -1);
}
