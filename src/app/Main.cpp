#include <juce_gui_extra/juce_gui_extra.h>
#include "MainWindow.h"
#include "MicrophonePermission.h"
#include "plugins/OutOfProcessPluginScanner.h"

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

    // Normally single-instance — but the out-of-process plugin scanner relaunches this
    // same binary as a worker (its command line carries the scanner id). That second
    // launch must be allowed through the single-instance lock, or scanning deadlocks.
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains (kScannerProcessUID);
    }

    void initialise (const juce::String& commandLine) override
    {
        // If this process was launched as a plugin-scan worker, run ONLY that — no
        // window, no audio engine, no mic prompt. Keep the handle alive for its lifetime.
        if (auto worker = startScannerSubprocessIfRequested (commandLine))
        {
            scannerWorker = std::move (worker);
            return;
        }

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
    std::shared_ptr<void>       scannerWorker;   // alive only in the scan-worker subprocess
};

//==============================================================================
// JUCE application entry point macro
START_JUCE_APPLICATION (CloudesApplication)
