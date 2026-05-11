#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * CloudesUIBehaviour — custom Tracktion Engine UIBehaviour.
 *
 * The default te::UIBehaviour::runTaskWithProgressBar() calls jassertfalse
 * and does nothing — te::Renderer::renderToFile() calls it to run the render
 * task, so without this override, all exports silently produce no output.
 *
 * Implementation mirrors the TE TestRunner example:
 *   - Spawns a juce::Thread (TaskRunner) that calls task.runJob() in a loop
 *   - Pumps the JUCE message dispatch loop (10ms slices) on the calling thread
 *     so the UI stays responsive during render
 *
 * Called from the message thread by ExportDialog::startExport().
 *
 * Reference: extern/tracktion_engine/examples/TestRunner/TestRunner.h
 */
class CloudesUIBehaviour : public te::UIBehaviour
{
public:
    // -------------------------------------------------------------------------
    // Called by te::Renderer::renderToFile — runs the task on a background
    // thread while keeping the message loop alive.
    // -------------------------------------------------------------------------
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        struct TaskRunner : public juce::Thread
        {
            explicit TaskRunner (te::ThreadPoolJobWithProgress& t)
                : juce::Thread (t.getJobName()), task (t)
            {
                startThread();
            }

            ~TaskRunner() override
            {
                task.signalJobShouldExit();
                waitForThreadToExit (10000);
            }

            void run() override
            {
                while (! threadShouldExit())
                    if (task.runJob() == juce::ThreadPoolJob::jobHasFinished)
                        return;
            }

            te::ThreadPoolJobWithProgress& task;
        };

        TaskRunner runner (task);

        // Pump the message dispatch loop (10ms slices) until the TaskRunner
        // finishes — keeps UI responsive and allows progress repaints.
        while (runner.isThreadRunning())
            if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
                break;
    }

    // TE calls this when recording is rejected (e.g. no armed inputs).
    // Default UIBehaviour shows nothing — override to make the warning visible.
    void showWarningMessage (const juce::String& msg) override
    {
        juce::MessageManager::callAsync ([msg]
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Cloudes", msg);
        });
    }
};
