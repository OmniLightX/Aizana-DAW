#pragma once

#include "engine/EditManager.h"
#include "audio/AudioEngine.h"

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
    AudioEngine audioEngine;

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
