#include "OutOfProcessPluginScanner.h"
#include "ClapPluginFormat.h"
#include "BuiltInPluginFormat.h"

#include <mutex>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>

// Worker self-watchdog: if probing one plugin blocks the worker's message thread for
// longer than this (a plugin that HANGS during instantiation — e.g. some heavy VST3/AU
// that spins or pops a modal), the worker hard-exits its own process. The coordinator
// then sees a dropped connection and blacklists that plugin. This is what prevents hung
// workers from becoming un-killable zombies (a graceful kill never reaches a blocked
// message thread).
//
// Tuned generously (60s) for large libraries: heavyweight-but-VALID plugins (Kontakt,
// Omnisphere, big Komplete instruments) can legitimately take >25s to return their
// descriptor, and we must NOT falsely blacklist them. The cost is that a genuinely hung
// plugin stalls for up to 60s before the scan moves on — acceptable for a one-time,
// cached scan.
static constexpr int kWorkerScanTimeoutMs = 60000;

// Coordinator-side backstop, slightly longer so the worker watchdog fires first and we
// get a clean connection-lost. If even that fails, the coordinator gives up on the
// plugin after this and respawns a fresh worker.
static constexpr int kPerPluginTimeoutMs = 70000;

// Host-side cap for the IN-PROCESS fallback (used only when the worker process can't be
// launched/reached at all). Here there is no child to kill — a hung plugin can only be
// escaped by abandoning the probe thread — so keep this shorter than the worker watchdog.
static constexpr int kInProcessFallbackTimeoutMs = 20000;

// ---------------------------------------------------------------------------
// Shared format registration — identical on both sides so format names match.
// ---------------------------------------------------------------------------
void registerScannableFormats (juce::AudioPluginFormatManager& fm)
{
    fm.addDefaultFormats();                     // VST3 (+ AudioUnit on macOS)
    fm.addFormat (new ClapPluginFormat());      // CLAP via the direct C API
    fm.addFormat (new BuiltInPluginFormat());   // Aizana' own FX (synthetic — never crashes)
}

// ===========================================================================
// Worker side — runs inside the child process, probes one plugin at a time.
// Mirrors JUCE's AudioPluginHost PluginScannerSubprocess.
// ===========================================================================
namespace
{
class PluginScannerSubprocess final : private juce::ChildProcessWorker,
                                      private juce::AsyncUpdater
{
public:
    PluginScannerSubprocess() { registerScannableFormats (formatManager); }

    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty())
            return;

        const std::lock_guard<std::mutex> lock (mutex);

        if (auto results = doScan (mb); ! results.isEmpty())
        {
            sendResults (results);
        }
        else
        {
            // doScan ran off the message thread (or found nothing) — retry on the
            // message thread, where AU/VST3 instantiation is legal.
            pendingBlocks.emplace (mb);
            triggerAsyncUpdate();
        }
    }

    void handleConnectionLost() override
    {
        // Coordinator went away (e.g. scan finished/cancelled) — this worker is done.
        juce::JUCEApplicationBase::quit();
    }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);

            if (pendingBlocks.empty())
                return;

            sendResults (doScan (pendingBlocks.front()));
            pendingBlocks.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> doScan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream { block, false };
        const auto formatName = stream.readString();
        const auto identifier = stream.readString();

        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        pd.uniqueId = pd.deprecatedUid = 0;

        juce::AudioPluginFormat* matchingFormat = nullptr;
        for (auto* format : formatManager.getFormats())
            if (format->getName() == formatName)
            {
                matchingFormat = format;
                break;
            }

        juce::OwnedArray<juce::PluginDescription> results;

        if (matchingFormat != nullptr
            && (juce::MessageManager::getInstance()->isThisTheMessageThread()
                || matchingFormat->requiresUnblockedMessageThreadDuringCreation (pd)))
        {
            // Arm a watchdog on a SEPARATE thread before the probe. findAllTypesForFile
            // runs on (and may block) this message thread, so an in-loop timer would
            // never fire — only an independent thread can react to a hang. If the probe
            // doesn't finish in time, hard-exit the whole worker process.
            std::mutex m;
            std::condition_variable cv;
            bool finished = false;

            std::thread watchdog ([&]
            {
                std::unique_lock<std::mutex> lk (m);
                if (! cv.wait_for (lk, std::chrono::milliseconds (kWorkerScanTimeoutMs),
                                   [&] { return finished; }))
                    std::_Exit (99);   // plugin hung -> drop dead so the coordinator blacklists it
            });

            // THIS is the line that may crash or hang on a broken plugin — and that's
            // fine: it kills only this child process, never the host.
            matchingFormat->findAllTypesForFile (results, identifier);

            {
                const std::lock_guard<std::mutex> lk (m);
                finished = true;
            }
            cv.notify_all();
            watchdog.join();
        }

        return results;
    }

    void sendResults (const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml ("LIST");

        for (const auto& desc : results)
            xml.addChildElement (desc->createXml().release());

        const auto str = xml.toString();
        sendMessageToCoordinator ({ str.toRawUTF8(), str.getNumBytesAsUTF8() });
    }

    std::mutex                                mutex;
    std::queue<juce::MemoryBlock>             pendingBlocks;
    juce::AudioPluginFormatManager            formatManager;
};

struct ScannerSubprocessHandle
{
    std::unique_ptr<PluginScannerSubprocess> worker;
};
} // namespace

std::shared_ptr<void> startScannerSubprocessIfRequested (const juce::String& commandLine)
{
    auto worker = std::make_unique<PluginScannerSubprocess>();

    if (worker->initialiseFromCommandLine (commandLine, kScannerProcessUID))
    {
        auto handle = std::make_shared<ScannerSubprocessHandle>();
        handle->worker = std::move (worker);
        return handle;   // shared_ptr<void> keeps the complete-type deleter
    }

    return nullptr;
}

// ===========================================================================
// Coordinator side — launches/owns one worker process and talks to it.
// ===========================================================================
namespace
{
class Superprocess final : private juce::ChildProcessCoordinator
{
public:
    Superprocess()
    {
        launchedOk = launchWorkerProcess (
            juce::File::getSpecialLocation (juce::File::currentExecutableFile),
            kScannerProcessUID, 0, 0);
    }

    bool isUsable() const noexcept { return launchedOk; }

    enum class State { timeout, gotResult, connectionLost };

    struct Response
    {
        State state;
        std::unique_ptr<juce::XmlElement> xml;
    };

    Response getResponse()
    {
        std::unique_lock<std::mutex> lock { mutex };

        if (! condvar.wait_for (lock, std::chrono::milliseconds { 50 },
                                [&] { return gotResult || connectionLost; }))
            return { State::timeout, nullptr };

        const auto state = connectionLost ? State::connectionLost : State::gotResult;
        connectionLost = false;
        gotResult      = false;

        return { state, std::move (pluginDescription) };
    }

    using juce::ChildProcessCoordinator::sendMessageToWorker;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        pluginDescription = juce::parseXML (mb.toString());
        gotResult = true;
        condvar.notify_one();
    }

    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock { mutex };
        connectionLost = true;
        condvar.notify_one();
    }

    std::mutex                        mutex;
    std::condition_variable           condvar;
    std::unique_ptr<juce::XmlElement> pluginDescription;
    bool                              connectionLost = false;
    bool                              gotResult      = false;
    bool                              launchedOk     = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Superprocess)
};

class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    OutOfProcessScanner() = default;   // JUCE_DECLARE_NON_COPYABLE suppresses the implicit one

    bool findPluginTypesFor (juce::AudioPluginFormat& format,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        // Built-in Aizana FX are synthetic descriptions (no binary to load, can't hang).
        if (format.getName() == "Built-in")
        {
            format.findAllTypesForFile (result, fileOrIdentifier);
            return true;
        }

        // Worker infra already declared bad — don't keep paying to relaunch it. Probe
        // in-process, but ALWAYS under a watchdog: with no child process to kill, a
        // synchronous in-process probe of a hanging plugin (AUs are the worst offenders)
        // would otherwise freeze the whole scan thread permanently.
        if (infraGivenUp.load())
            return scanInProcessWithWatchdog (format, fileOrIdentifier, result);

        switch (scanOutOfProcess (format.getName(), fileOrIdentifier, result))
        {
            case Outcome::ok:
                return true;

            case Outcome::pluginCrashed:
                superprocess = nullptr;     // respawn a clean worker for the next plugin
                return false;               // -> KnownPluginList blacklists this plugin

            case Outcome::infraUnavailable:
            default:
                // Could not even launch/reach a worker — degrade to in-process scanning
                // (no worse than the legacy behaviour) instead of blacklisting good plugins,
                // but bounded by the watchdog so one hang can't stall the scan forever.
                infraGivenUp = true;
                superprocess = nullptr;
                return scanInProcessWithWatchdog (format, fileOrIdentifier, result);
        }
    }

    void scanFinished() override { superprocess = nullptr; }

private:
    enum class Outcome { ok, pluginCrashed, infraUnavailable };

    // In-process probe bounded by a watchdog. findAllTypesForFile loads the plugin on a
    // throwaway thread; if it doesn't finish before the deadline we abandon that thread
    // and skip the plugin — the scan must never freeze. The descriptors are collected in
    // a shared heap buffer so an abandoned (hung) thread can never write into the caller's
    // stack-owned `result`. The probe captures the format by POINTER (the formatManager
    // owns it and outlives the scan); a detached thread is reclaimed at process exit.
    bool scanInProcessWithWatchdog (juce::AudioPluginFormat& format,
                                    const juce::String& fileOrIdentifier,
                                    juce::OwnedArray<juce::PluginDescription>& result)
    {
        auto buffer = std::make_shared<juce::OwnedArray<juce::PluginDescription>>();
        auto done   = std::make_shared<std::atomic<bool>> (false);

        std::thread probe ([fmt = &format, id = fileOrIdentifier, buffer, done]
        {
            // Guard: an exception escaping a std::thread body calls std::terminate. The
            // synchronous path was covered by PluginScanner's try/catch; preserve that.
            try { fmt->findAllTypesForFile (*buffer, id); }
            catch (...) {}
            done->store (true, std::memory_order_release);
        });

        const auto deadline = std::chrono::steady_clock::now()
                                  + std::chrono::milliseconds (kInProcessFallbackTimeoutMs);
        while (! done->load (std::memory_order_acquire)
               && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for (std::chrono::milliseconds (25));

        if (done->load (std::memory_order_acquire))
        {
            probe.join();
            result.swapWith (*buffer);
            return true;
        }

        probe.detach();   // hung past the deadline — abandon and move on
        return false;
    }

    Outcome scanOutOfProcess (const juce::String& formatName,
                              const juce::String& fileOrIdentifier,
                              juce::OwnedArray<juce::PluginDescription>& result)
    {
        if (superprocess == nullptr)
            superprocess = std::make_unique<Superprocess>();

        if (! superprocess->isUsable())
            return Outcome::infraUnavailable;

        juce::MemoryBlock block;
        juce::MemoryOutputStream stream { block, true };
        stream.writeString (formatName);
        stream.writeString (fileOrIdentifier);
        stream.flush();

        if (! superprocess->sendMessageToWorker (block))
            return Outcome::infraUnavailable;

        // Per-plugin wall-clock deadline: a plugin that *hangs* the worker (e.g. a modal
        // dialog or infinite loop during instantiation) never drops the IPC ping, so the
        // connection-lost timeout alone won't recover it. After this deadline we give up
        // on the plugin, which kills the worker and blacklists it — so one stuck plugin
        // can't stall the whole scan (some AUs do exactly this).
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) kPerPluginTimeoutMs;

        for (;;)
        {
            if (shouldExit())
                return Outcome::ok;

            if (juce::Time::getMillisecondCounter() > deadline)
                return Outcome::pluginCrashed;   // hung -> kill worker + blacklist, then move on

            const auto response = superprocess->getResponse();

            if (response.state == Superprocess::State::timeout)
                continue;

            if (response.xml != nullptr)
                for (const auto* item : response.xml->getChildIterator())
                {
                    auto desc = std::make_unique<juce::PluginDescription>();
                    if (desc->loadFromXml (*item))
                        result.add (std::move (desc));
                }

            return response.state == Superprocess::State::gotResult ? Outcome::ok
                                                                     : Outcome::pluginCrashed;
        }
    }

    std::unique_ptr<Superprocess> superprocess;
    std::atomic<bool>             infraGivenUp { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessScanner)
};
} // namespace

void installOutOfProcessScanner (juce::KnownPluginList& list)
{
    list.setCustomScanner (std::make_unique<OutOfProcessScanner>());
}
