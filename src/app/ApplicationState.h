#pragma once

#include "engine/EditManager.h"
#include "engine/AICommandBridge.h"
#include "audio/AudioEngine.h"
#include "UIStateManager.h"
#include "ai/IntentEngine.h"

/**
 * ApplicationState — Top-level owner of the two foundational singletons.
 *
 * Owns both EditManager and AudioEngine. Constructed once by MainWindow on startup.
 * All plans 01-03 through 01-11 receive a reference to ApplicationState for access
 * to the current Edit and audio device configuration.
 *
 * Design: simple aggregate struct (not a singleton pattern) — the owner (MainWindow)
 * holds the unique_ptr<ApplicationState> and passes references to sub-components.
 *
 * Startup sequence:
 *   1. AudioEngine::initialise() — open audio device (non-fatal on failure)
 *   2. EditManager::createNewEdit() — create a fresh session
 */
struct ApplicationState
{
    EditManager editManager;
    AudioEngine audioEngine { editManager.getEngine() };

    // AI→TE command queue bridge — the ONLY path for AI-driven Edit mutations
    // (background AI thread enqueues, message thread applies; see AICommandBridge).
    // Phase 2 core: instantiated and ready for the Phase 3 AI worker.
    AICommandBridge aiCommandBridge { editManager };

    // "Das Dial" — the single source of truth for UIMode + assist prominence + work
    // context (Phase 2, Block A). Reads any persisted mode/assist on construction.
    UIStateManager uiState { editManager.getEngine() };

    // The translator (Phase 2, Block B) — intent -> editable TE command batches,
    // applied via aiCommandBridge as one undo step. Heuristic for now; ML in Phase 3.
    IntentEngine intentEngine { editManager };

    ApplicationState()
    {
        // 1. Open audio device — failure is non-fatal (user may have no audio hardware,
        //    or be in a test/CI environment). Log the error for the preferences dialog.
        auto err = audioEngine.initialise();
        if (err.isNotEmpty())
            juce::Logger::writeToLog ("AudioEngine init warning: " + err);

        // 2. Create a new empty Edit — this also sets up the shared UndoManager.
        editManager.createNewEdit();
    }
};
