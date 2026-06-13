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
 * Implementation:
 *   - Spawns a juce::Thread (TaskRunner) that calls task.runJob() in a loop
 *   - PUMPS the JUCE message dispatch loop (not a bare sleep) on the calling
 *     thread so the UI stays alive AND the render thread's message-thread
 *     dependencies (3rd-party plugin prepareToPlay, async updates) get serviced.
 *     A bare sleep here DEADLOCKS those dependencies and freezes the whole app —
 *     that was the cause of the track-freeze hang (offline render of an Eiosis
 *     plugin needs the message thread; the old sleep loop starved it).
 *   - Stall timeout: if render progress stops advancing for kStallTimeoutMs, the
 *     task is asked to exit, so a misbehaving offline plugin can't permanently
 *     hang the app (a true fix for hard-stuck plugins is out-of-process hosting).
 *
 * Called from the message thread by ExportDialog::startExport() and by track
 * freeze (AudioTrack::setFrozen → freezeTrack → Renderer::renderToFile).
 * Requires JUCE_MODAL_LOOPS_PERMITTED=1 (set in CMakeLists.txt).
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

        auto* mm = juce::MessageManager::getInstanceWithoutCreating();

        // Stall watchdog: track the last time render progress advanced. If it
        // stops advancing for this long, the render is stuck (e.g. a 3rd-party
        // plugin misbehaving offline) → ask it to exit so the app survives.
        constexpr juce::uint32 kStallTimeoutMs = 30000;
        float        lastProgress   = -1.0f;
        juce::uint32 lastAdvanceTime = juce::Time::getMillisecondCounter();

        while (runner.isThreadRunning())
        {
            // Pump the message loop (NOT a bare sleep) so the render thread's
            // message-thread dependencies are serviced — prevents the deadlock
            // that froze the whole app. Falls back to sleep only if there is no
            // message manager (e.g. headless test context).
            if (mm != nullptr)
                mm->runDispatchLoopUntil (20);
            else
                juce::Thread::sleep (20);

            const float p = task.getCurrentTaskProgress();
            const auto  now = juce::Time::getMillisecondCounter();

            if (p > lastProgress + 1.0e-4f)
            {
                lastProgress    = p;
                lastAdvanceTime = now;
            }
            else if (now - lastAdvanceTime > kStallTimeoutMs)
            {
                // Cooperative abort — the TE render task checks shouldExit
                // between blocks, so it unwinds cleanly; TaskRunner's dtor then
                // joins it. (A plugin hard-stuck inside one processBlock still
                // needs out-of-process hosting — Phase 4.)
                task.signalJobShouldExit();
                break;
            }
        }
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
