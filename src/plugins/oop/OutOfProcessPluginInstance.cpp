#include "OutOfProcessPluginInstance.h"
#include "PluginHostBridge.h"
#include "app/LegacyMigration.h"

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

        // Metadata for one child-plugin parameter, shipped host←worker in the "info" reply.
        struct OopParamInfo
        {
            juce::String name, label;
            float defaultValue = 0.0f;
            int   numSteps = 0;
            bool  discrete = false, automatable = true;
        };

        struct OopPluginInfo
        {
            bool acceptsMidi = false, producesMidi = false;
            juce::Array<OopParamInfo> params;
        };

        // ===================================================================
        // Worker side — runs in the child process, hosts one plugin.
        //
        // E5.4 message protocol (every message starts with a type string):
        //   host → worker : "describe"(descXml) | "prepare"(ring,rate,block,ch) | "unprepare"
        //                   | "getstate" | "setstate"(size,bytes)
        //   worker → host : "info"(flags+params) | "error" | "ready" | "state"(size,bytes)
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
                juce::MemoryInputStream in (mb, false);
                const auto type = in.readString();

                if (type == "describe")   handleDescribe (in);
                else if (type == "prepare")    handlePrepare (in);
                else if (type == "unprepare")  handleUnprepare();
                else if (type == "getstate")   handleGetState();
                else if (type == "setstate")   handleSetState (in);
            }

            void handleDescribe (juce::MemoryInputStream& in)
            {
                if (plugin != nullptr)   // one plugin per worker process
                    return;

                const auto descXml = in.readString();

                juce::PluginDescription desc;
                if (auto xml = juce::parseXML (descXml))
                    desc.loadFromXml (*xml);

                juce::String err;
                // Rate/block here are only placeholders for loading; the real values arrive
                // with "prepare" (prepareToPlay is re-run there).
                plugin = loader ? loader (desc, 44100.0, 512, err) : nullptr;
                if (plugin == nullptr) { replyError(); return; }

                juce::MemoryBlock out;
                juce::MemoryOutputStream os (out, false);
                os.writeString ("info");
                os.writeBool (plugin->acceptsMidi());
                os.writeBool (plugin->producesMidi());

                const auto& params = plugin->getParameters();
                os.writeInt (params.size());
                for (auto* p : params)
                {
                    os.writeString (p->getName (256));
                    os.writeString (p->getLabel());
                    os.writeFloat  (p->getDefaultValue());
                    os.writeInt    (p->getNumSteps());
                    os.writeBool   (p->isDiscrete());
                    os.writeBool   (p->isAutomatable());
                }
                os.flush();
                sendMessageToCoordinator (out);
            }

            void handlePrepare (juce::MemoryInputStream& in)
            {
                const auto ringNm = in.readString();
                const auto rate   = in.readDouble();
                const auto block  = in.readInt();
                in.readInt();   // numChannels — the ring already carries the capacity

                if (plugin == nullptr) { replyError(); return; }

                stopPump();   // re-prepare replaces any previous ring/pump

                ring = SharedAudioRing::attachChild (ringNm);
                if (ring == nullptr) { replyError(); return; }

                plugin->setRateAndBufferSizeDetails (rate, block);
                plugin->prepareToPlay (rate, block);

                stopFlag.store (false);
                pump = std::thread ([this]
                {
                    PluginHostBridge::run (*ring, *plugin, [this] { return stopFlag.load(); });
                });

                juce::MemoryBlock out;
                juce::MemoryOutputStream os (out, false);
                os.writeString ("ready");   // pump is constructed before we report ready
                os.flush();
                sendMessageToCoordinator (out);
            }

            void handleUnprepare()
            {
                stopPump();
                ring.reset();
                if (plugin != nullptr)
                    plugin->releaseResources();
            }

            void handleGetState()
            {
                juce::MemoryBlock state;
                if (plugin != nullptr)
                    plugin->getStateInformation (state);

                juce::MemoryBlock out;
                juce::MemoryOutputStream os (out, false);
                os.writeString ("state");
                os.writeInt64 ((juce::int64) state.getSize());
                os.write (state.getData(), state.getSize());
                os.flush();
                sendMessageToCoordinator (out);
            }

            void handleSetState (juce::MemoryInputStream& in)
            {
                const auto size = (size_t) in.readInt64();
                juce::MemoryBlock state;
                state.setSize (size);
                if (in.read (state.getData(), (int) size) == (int) size && plugin != nullptr)
                    plugin->setStateInformation (state.getData(), (int) size);

                // Ack so the host can treat setState as synchronous — a session load must not
                // race the first processBlock against the state application.
                juce::MemoryBlock out;
                juce::MemoryOutputStream os (out, false);
                os.writeString ("stateset");
                os.flush();
                sendMessageToCoordinator (out);
            }

            void handleConnectionLost() override
            {
                stopPump();
                finished.store (true);
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();   // app worker quits; console worker polls isFinished()
            }

            void replyError()
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("error");
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
        };

        // ===================================================================
        // Coordinator side — host process owns + talks to one worker.
        // Requests are strictly sequential (the host waits for each reply), so plain
        // fields + one WaitableEvent per reply type are race-free.
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

            bool isUsable() const noexcept { return launchedOk && ! lost.load(); }

            bool describe (const juce::String& descXml, OopPluginInfo& outInfo)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("describe");
                out.writeString (descXml);
                out.flush();
                infoEvent.reset();
                sendMessageToWorker (mb);

                // Plugin loading in the child can take a moment (CLAP dlopen + factory).
                if (! infoEvent.wait (10000) || ! infoOk.load())
                    return false;

                outInfo = info;
                return true;
            }

            bool prepare (const juce::String& ringName, double rate, int block, int numChannels)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("prepare");
                out.writeString (ringName);
                out.writeDouble (rate);
                out.writeInt (block);
                out.writeInt (numChannels);
                out.flush();
                readyEvent.reset();
                ready.store (false);
                sendMessageToWorker (mb);
                return readyEvent.wait (5000) && ready.load();
            }

            void unprepare()
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("unprepare");
                out.flush();
                sendMessageToWorker (mb);
            }

            bool getState (juce::MemoryBlock& outState)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("getstate");
                out.flush();
                stateEvent.reset();
                stateOk.store (false);
                sendMessageToWorker (mb);

                if (! stateEvent.wait (3000) || ! stateOk.load())
                    return false;

                outState = stateBlock;
                return true;
            }

            void setState (const void* data, int size)
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream out (mb, false);
                out.writeString ("setstate");
                out.writeInt64 ((juce::int64) size);
                out.write (data, (size_t) juce::jmax (0, size));
                out.flush();
                stateSetEvent.reset();
                sendMessageToWorker (mb);
                stateSetEvent.wait (3000);   // synchronous: applied before the next block
            }

        private:
            void handleMessageFromWorker (const juce::MemoryBlock& mb) override
            {
                juce::MemoryInputStream in (mb, false);
                const auto type = in.readString();

                if (type == "info")
                {
                    OopPluginInfo parsed;
                    parsed.acceptsMidi  = in.readBool();
                    parsed.producesMidi = in.readBool();

                    const int n = juce::jlimit (0, 100000, in.readInt());
                    for (int i = 0; i < n && ! in.isExhausted(); ++i)
                    {
                        OopParamInfo p;
                        p.name         = in.readString();
                        p.label        = in.readString();
                        p.defaultValue = in.readFloat();
                        p.numSteps     = in.readInt();
                        p.discrete     = in.readBool();
                        p.automatable  = in.readBool();
                        parsed.params.add (std::move (p));
                    }

                    info = std::move (parsed);
                    infoOk.store (true);
                    infoEvent.signal();
                }
                else if (type == "ready" || type == "error")
                {
                    // "error" during describe must also unblock that waiter.
                    ready.store (type == "ready");
                    infoOk.store (false);
                    readyEvent.signal();
                    infoEvent.signal();
                }
                else if (type == "state")
                {
                    const auto size = (size_t) in.readInt64();
                    stateBlock.setSize (size);
                    stateOk.store (in.read (stateBlock.getData(), (int) size) == (int) size);
                    stateEvent.signal();
                }
                else if (type == "stateset")
                {
                    stateSetEvent.signal();
                }
            }

            void handleConnectionLost() override
            {
                lost.store (true);
                ready.store (false);
                infoOk.store (false);
                stateOk.store (false);
                readyEvent.signal();   // unblock any waiter if the worker died
                infoEvent.signal();
                stateEvent.signal();
                stateSetEvent.signal();
            }

            bool                    launchedOk { false };
            std::atomic<bool>       lost { false };
            std::atomic<bool>       ready { false };
            std::atomic<bool>       infoOk { false };
            std::atomic<bool>       stateOk { false };
            juce::WaitableEvent     readyEvent, infoEvent, stateEvent, stateSetEvent;
            OopPluginInfo           info;
            juce::MemoryBlock       stateBlock;
        };

        // ===================================================================
        // Host-side parameter proxy (E5.4). setValue queues a change into the owner's
        // lock-free FIFO; the audio thread drains it into the ring's param region, and the
        // child applies it to the real plugin before the next block.
        // ===================================================================
        class OopParameterProxy final : public juce::AudioPluginInstance::HostedParameter
        {
        public:
            OopParameterProxy (OutOfProcessPluginInstance& o, uint32_t idx, OopParamInfo i)
                : owner (o), index (idx), meta (std::move (i)), value (meta.defaultValue) {}

            juce::String getParameterID() const override { return "oop" + juce::String ((int) index); }

            float getValue() const override               { return value.load(); }
            void  setValue (float v) override
            {
                value.store (v);
                owner.queueParamChange (index, v);
            }

            float getDefaultValue() const override        { return meta.defaultValue; }
            juce::String getName (int maxLen) const override { return meta.name.substring (0, maxLen); }
            juce::String getLabel() const override        { return meta.label; }
            int  getNumSteps() const override             { return meta.numSteps > 0 ? meta.numSteps
                                                                : juce::AudioProcessorParameter::getNumSteps(); }
            bool isDiscrete() const override              { return meta.discrete; }
            bool isAutomatable() const override           { return meta.automatable; }

            juce::String getText (float v, int) const override { return juce::String (v, 3); }
            float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }

        private:
            OutOfProcessPluginInstance& owner;
            const uint32_t              index;
            const OopParamInfo          meta;
            std::atomic<float>          value;
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
        // Spawn + describe up front: TE builds its automation parameter list right after
        // instantiation, so the proxies must exist before this constructor returns.
        ensureCoordinator();
    }

    OutOfProcessPluginInstance::~OutOfProcessPluginInstance()
    {
        teardown();
        if (impl != nullptr)
            impl->coordinator.reset();   // drops the worker connection → worker exits cleanly
    }

    bool OutOfProcessPluginInstance::ensureCoordinator()
    {
        if (impl->coordinator != nullptr && impl->coordinator->isUsable())
            return true;

        impl->coordinator = std::make_unique<PluginHostCoordinator>();
        if (! impl->coordinator->isUsable())
        {
            impl->coordinator.reset();
            return false;
        }

        OopPluginInfo info;
        if (! impl->coordinator->describe (descToXml (desc), info))
        {
            impl->coordinator.reset();
            return false;
        }

        childAcceptsMidi  = info.acceptsMidi;
        childProducesMidi = info.producesMidi;

        // Parameters can only be added once per AudioProcessor — on a respawn after a child
        // death the existing proxies stay valid (indices are stable per description).
        if (! paramsBuilt)
        {
            for (int i = 0; i < info.params.size(); ++i)
                addHostedParameter (std::make_unique<OopParameterProxy> (
                    *this, (uint32_t) i, info.params.getReference (i)));
            paramsBuilt = true;
        }

        return true;
    }

    void OutOfProcessPluginInstance::prepareToPlay (double sampleRate, int blockSize)
    {
        teardown();

        if (! ensureCoordinator())
            return;   // childReady stays false → processBlock silences

        numCh = juce::jmax (1, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));

        ring = SharedAudioRing::createHost ((uint32_t) numCh, (uint32_t) juce::jmax (1, blockSize), ringName);
        if (ring == nullptr)
            return;

        childReady.store (impl->coordinator->prepare (ringName, sampleRate, blockSize, numCh));

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

    void OutOfProcessPluginInstance::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        if (! childReady.load() || ring == nullptr)
        {
            buffer.clear();
            midi.clear();
            return;
        }

        // Drain queued parameter changes into the ring (host audio thread, lock-free).
        {
            OopParamEvent events[SharedAudioRing::kMaxParamEvents];
            uint32_t count = 0;

            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            paramFifo.prepareToRead (paramFifo.getNumReady(), start1, size1, start2, size2);

            for (int i = 0; i < size1 && count < SharedAudioRing::kMaxParamEvents; ++i)
                events[count++] = paramSlots[start1 + i];
            for (int i = 0; i < size2 && count < SharedAudioRing::kMaxParamEvents; ++i)
                events[count++] = paramSlots[start2 + i];

            paramFifo.finishedRead (size1 + size2);
            OobAudioMarshal::writeParamEvents (*ring, events, count);
        }

        OobAudioMarshal::hostProcessBlock (*ring, buffer, midi, timeoutMicros);
    }

    void OutOfProcessPluginInstance::queueParamChange (uint32_t index, float value) noexcept
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        paramFifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 > 0)
            paramSlots[start1] = { index, value };

        paramFifo.finishedWrite (size1);   // FIFO full → drop; the child keeps its last value
    }

    void OutOfProcessPluginInstance::getStateInformation (juce::MemoryBlock& mb)
    {
        if (impl->coordinator != nullptr && impl->coordinator->isUsable())
            impl->coordinator->getState (mb);
    }

    void OutOfProcessPluginInstance::setStateInformation (const void* data, int size)
    {
        if (data != nullptr && size > 0
             && impl->coordinator != nullptr && impl->coordinator->isUsable())
            impl->coordinator->setState (data, size);
    }

    void OutOfProcessPluginInstance::releaseResources()
    {
        teardown();
    }

    void OutOfProcessPluginInstance::teardown()
    {
        childReady.store (false);

        // Stop the child's pump BEFORE dropping the host ring: the worker keeps its own
        // mapping alive, so this is about protocol hygiene, not memory safety.
        if (impl != nullptr && impl->coordinator != nullptr && impl->coordinator->isUsable())
            impl->coordinator->unprepare();

        ring.reset();
    }

    //==============================================================================
    bool oopHostingEnabled()
    {
        // Pre-rename flag honoured as fallback (Klasse C: scripts/docs/muscle-memory).
        return legacy::envWithLegacyFallback ("AIZANA_OOP_PLUGINS", "CLOUDES_OOP_PLUGINS", "0")
                   .trim() == "1";
    }

    void OutOfProcessPluginFormat::createPluginInstance (const juce::PluginDescription& desc,
                                                         double, int, PluginCreationCallback callback)
    {
        if (callback)
            callback (std::make_unique<OutOfProcessPluginInstance> (desc, 2), {});
    }
}
