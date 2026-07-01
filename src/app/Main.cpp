#include <juce_gui_extra/juce_gui_extra.h>
#include "MainWindow.h"
#include "MicrophonePermission.h"
#include "plugins/OutOfProcessPluginScanner.h"
#include "plugins/oop/OutOfProcessPluginInstance.h"

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
        const auto cmd = getCommandLineParameters();
        return cmd.contains (kScannerProcessUID)
            || cmd.contains (cloudes::kPluginHostProcessUID);   // out-of-process plugin host worker
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

        // Out-of-process plugin host worker (E5.3b): this process hosts ONE real plugin for the
        // main app and pumps its audio over a SharedAudioRing. Load via a fresh format manager
        // (CLAP/VST3/AU/Built-in). Like the scanner worker: no window, no engine, no mic prompt.
        if (auto host = cloudes::startPluginHostWorkerIfRequested (commandLine,
                [] (const juce::PluginDescription& d, double rate, int block, juce::String& err)
                    -> std::unique_ptr<juce::AudioProcessor>
                {
                    juce::AudioPluginFormatManager fm;
                    registerScannableFormats (fm);

                    if (auto inst = fm.createPluginInstance (d, rate, block, err))
                        return inst;

                    // The description may be OOP-wrapped (pluginFormatName == kOopFormatName),
                    // which fm doesn't know. Resolve the REAL plugin by its file path instead.
                    for (auto* f : fm.getFormats())
                        if (f != nullptr && f->fileMightContainThisPluginType (d.fileOrIdentifier))
                        {
                            juce::OwnedArray<juce::PluginDescription> found;
                            f->findAllTypesForFile (found, d.fileOrIdentifier);
                            if (! found.isEmpty())
                                if (auto inst = fm.createPluginInstance (*found.getFirst(), rate, block, err))
                                    return inst;
                        }
                    return nullptr;
                }))
        {
            pluginHostWorker = std::move (host);
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
    std::shared_ptr<void>       scannerWorker;      // alive only in the scan-worker subprocess
    std::shared_ptr<cloudes::PluginHostWorkerHandle> pluginHostWorker;   // alive only in a plugin-host worker
};

//==============================================================================
// JUCE application entry point macro
START_JUCE_APPLICATION (CloudesApplication)
