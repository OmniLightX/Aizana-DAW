#include "OutOfProcessPluginInstance.h"
#include "PluginHostBridge.h"
#include "OobAudioMarshal.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>

namespace aizana
{
    namespace
    {
        juce::String descToXml (const juce::PluginDescription& d)
        {
            if (auto xml = d.createXml())
                return xml->toString();
            return {};
        }

        // ===================================================================
        // Worker side — runs in the child process, hosts one plugin.
        // ===================================================================
        class PluginHostSubprocess final : private juce::ChildProcessWorker
        {
        public:
            explicit PluginHostSubprocess (PluginLoader l) : loader (std::move (l)) {}
            ~PluginHostSubprocess() override { stopPump(); }

            using juce::ChildProcessWorker::initialiseFromCommandLine;
            bool isFinished() const noexcept { return finished.load(); }

        private:
            void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
            {
                if (started)
                    return;   // one plugin per worker process

                juce::MemoryInputStream in (mb, false);
                const auto ringNm  = in.readString();
                const auto descXml = in.readString();
                const auto rate    = in.readDouble();
                const auto block   = in.readInt();
                in.readInt();   // numChannels — the ring already carries the capacity

                ring = SharedAudioRing::attachChild (ringNm);
                if (ring == nullptr) { reply (false); return; }

                juce::PluginDescription desc;
                if (auto xml = juce::parseXML (descXml))
                    desc.loadFromXml (*xml);

                juce::String err;
                plugin = loader ? loader (desc, rate, block, err) : nullptr;
                if (plugin == nullptr) { reply (false); return; }

                plugin->setRateAndBufferSizeDetails (rate, block);
                plugin->prepareToPlay (rate, block);

                started = true;
                pump = std::thread ([this]
                {
                    PluginHostBridge::run (*ring, *plugin, [this] { return stopFlag.load(); });
                });

                reply (true);   // pump is constructed before we report ready
            }

            void handleConnectionLost() override
            {
                stopPump();
                finished.store (true);
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();   // app worker quits; console worker polls isFinished()
            }

            void reply (bool ok)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString (ok ? "ready" : "error");
                out.flush();
                sendMessageToCoordinator (mb);
            }

            void stopPump()
            {
                stopFlag.store (true);
                if (pump.joinable())
                    pump.join();
            }

            PluginLoader                          loader;
            std::unique_ptr<SharedAudioRing>      ring;
            std::unique_ptr<juce::AudioProcessor> plugin;
            std::thread                           pump;
            std::atomic<bool>                     stopFlag { false };
            std::atomic<bool>                     finished { false };
            bool                                  started { false };
        };

        // ===================================================================
        // Coordinator side — host process owns + talks to one worker.
        // ===================================================================
        class PluginHostCoordinator final : private juce::ChildProcessCoordinator
        {
        public:
            PluginHostCoordinator()
            {
                launchedOk = launchWorkerProcess (
                    juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                    kPluginHostProcessUID, 5000, 5000);
            }

            bool isUsable() const noexcept { return launchedOk; }

            void sendConfig (const juce::String& ringName, const juce::String& descXml,
                             double rate, int block, int numChannels)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString (ringName);
                out.writeString (descXml);
                out.writeDouble (rate);
                out.writeInt (block);
                out.writeInt (numChannels);
                out.flush();
                sendMessageToWorker (mb);
            }

            bool waitForReady (int timeoutMs)
            {
                return readyEvent.wait (timeoutMs) && ready.load();
            }

        private:
            void handleMessageFromWorker (const juce::MemoryBlock& mb) override
            {
                juce::MemoryInputStream in (mb, false);
                ready.store (in.readString() == "ready");
                readyEvent.signal();
            }

            void handleConnectionLost() override
            {
                ready.store (false);
                readyEvent.signal();   // unblock a waiter if the worker died during launch
            }

            bool                    launchedOk { false };
            std::atomic<bool>       ready { false };
            juce::WaitableEvent     readyEvent;
        };
    } // namespace

    //==============================================================================
    std::shared_ptr<PluginHostWorkerHandle> startPluginHostWorkerIfRequested (
        const juce::String& commandLine, PluginLoader loader)
    {
        auto worker = std::make_unique<PluginHostSubprocess> (std::move (loader));

        if (worker->initialiseFromCommandLine (commandLine, kPluginHostProcessUID))
        {
            struct Handle final : PluginHostWorkerHandle
            {
                std::unique_ptr<PluginHostSubprocess> w;
                bool isFinished() const override { return w->isFinished(); }
            };
            auto h = std::make_shared<Handle>();
            h->w = std::move (worker);
            return h;
        }

        return nullptr;
    }

    //==============================================================================
    struct OutOfProcessPluginInstance::Impl
    {
        std::unique_ptr<PluginHostCoordinator> coordinator;
    };

    OutOfProcessPluginInstance::OutOfProcessPluginInstance (const juce::PluginDescription& d, int numChannels)
        : juce::AudioPluginInstance (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::canonicalChannelSet (juce::jmax (1, numChannels)), true)
              .withOutput ("Output", juce::AudioChannelSet::canonicalChannelSet (juce::jmax (1, numChannels)), true)),
          desc (d), numCh (juce::jmax (1, numChannels)), impl (std::make_unique<Impl>())
    {
    }

    OutOfProcessPluginInstance::~OutOfProcessPluginInstance()
    {
        teardown();
    }

    void OutOfProcessPluginInstance::prepareToPlay (double sampleRate, int blockSize)
    {
        teardown();

        numCh = juce::jmax (1, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));

        ring = SharedAudioRing::createHost ((uint32_t) numCh, (uint32_t) juce::jmax (1, blockSize), ringName);
        if (ring == nullptr)
            return;   // childReady stays false → processBlock silences

        impl->coordinator = std::make_unique<PluginHostCoordinator>();
        if (! impl->coordinator->isUsable())
        {
            impl->coordinator.reset();
            return;
        }

        impl->coordinator->sendConfig (ringName, descToXml (desc), sampleRate, blockSize, numCh);
        childReady.store (impl->coordinator->waitForReady (5000));

        // Wait up to ~4 block periods for a reply — a hung/crashed plugin is then silenced, the
        // host audio thread never blocking longer than this.
        const double safeRate = juce::jmax (8000.0, sampleRate);
        timeoutMicros = (uint32_t) juce::jmax (2000.0, (double) blockSize / safeRate * 1.0e6 * 4.0);

        // Warm up: the child's pump thread may not have flipped its alive flag the instant it
        // reported ready. Round-trip silence until it answers (or give up — processBlock then
        // silences safely). Also covers the ring's late-attach window.
        if (childReady.load() && ring != nullptr)
        {
            std::memset (ring->audioData(), 0, sizeof (float) * (size_t) numCh * ring->maxBlockSamples());
            for (int i = 0; i < 40; ++i)
            {
                if (ring->roundTrip ((uint32_t) numCh, 1, 5000) == SharedAudioRing::Status::ok)
                    break;
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        }
    }

    void OutOfProcessPluginInstance::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        if (! childReady.load() || ring == nullptr)
        {
            buffer.clear();
            return;
        }

        OobAudioMarshal::hostProcessBlock (*ring, buffer, timeoutMicros);
    }

    void OutOfProcessPluginInstance::releaseResources()
    {
        teardown();
    }

    void OutOfProcessPluginInstance::teardown()
    {
        childReady.store (false);
        if (impl != nullptr)
            impl->coordinator.reset();   // drops the worker connection → worker exits cleanly
        ring.reset();
    }

    //==============================================================================
    bool oopHostingEnabled()
    {
        return juce::SystemStats::getEnvironmentVariable ("AIZANA_OOP_PLUGINS", "0").trim() == "1";
    }

    void OutOfProcessPluginFormat::createPluginInstance (const juce::PluginDescription& desc,
                                                         double, int, PluginCreationCallback callback)
    {
        if (callback)
            callback (std::make_unique<OutOfProcessPluginInstance> (desc, 2), {});
    }
}
