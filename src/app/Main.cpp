#include <juce_gui_extra/juce_gui_extra.h>
#include "MainWindow.h"
#include "MicrophonePermission.h"

//==============================================================================
/**
 * CloudesApplication — JUCE application entry point for the Cloudes DAW.
 *
 * Phase 1 skeleton. Tracktion Engine initialisation, audio device setup,
 * and session management are added in plan 01-02 (Engine Core).
 */
class CloudesApplication : public juce::JUCEApplication
{
public:
    CloudesApplication() = default;

    const juce::String getApplicationName() override       { return "Cloudes"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override              { return false; }

    void initialise (const juce::String& /*commandLine*/) override
    {
        // Defer permission request to the first run-loop tick so NSApplication
        // is fully active before AVCaptureDevice shows the system dialog.
        juce::MessageManager::callAsync ([this]
        {
            requestMicrophonePermission ([this] (bool /*granted*/)
            {
                mainWindow = std::make_unique<MainWindow> (getApplicationName());
            });
        });
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& /*commandLine*/) override {}

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// JUCE application entry point macro
START_JUCE_APPLICATION (CloudesApplication)
