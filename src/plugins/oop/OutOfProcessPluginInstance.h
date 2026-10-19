#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedAudioRing.h"
#include "OobAudioMarshal.h"

#include <atomic>
#include <functional>
#include <memory>

/**
 * Out-of-process plugin hosting — host proxy + child worker (E5.3b).
 *
 * OutOfProcessPluginInstance is an AudioProcessor that hosts the REAL plugin in a separate
 * child process. Its processBlock marshals audio across a SharedAudioRing (E5.1) to a
 * PluginHostBridge (E5.2) running in the child; OobAudioMarshal (E5.3a) handles the host side
 * and silences the block if the child hangs or crashes — so a broken plugin can never freeze
 * or crash the DAW. Control/lifecycle ride a juce::ChildProcessCoordinator/Worker pair
 * (mirrors OutOfProcessPluginScanner): config in, ready/error back, connection-lost = exit.
 *
 * The worker turns a PluginDescription into a live AudioProcessor via an injected PluginLoader
 * (the app passes its format manager; the smoke test injects a stub) so the worker code carries
 * no plugin-format dependency of its own.
 */
namespace aizana
{
    // Command-line marker that launches a process in plugin-host worker mode (like the scanner).
    inline constexpr const char* kPluginHostProcessUID = "aizana_plugin_host";

    using PluginLoader = std::function<std::unique_ptr<juce::AudioProcessor> (
        const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error)>;

    // Descriptions whose pluginFormatName equals this route through OutOfProcessPluginFormat.
    inline constexpr const char* kOopFormatName = "Aizana OOP";

    /** True when out-of-process plugin hosting is enabled (env AIZANA_OOP_PLUGINS=1).
        Default OFF — in-process hosting stays the shipping path until E5 is complete. */
    bool oopHostingEnabled();

    /** E5.6: True when ALSO 3rd-party plugins (VST3/AU/CLAP) route out-of-process
        (env AIZANA_OOP_ALL=1; impliziert oopHostingEnabled). LIVE-TEST-only. */
    bool oopThirdPartyEnabled();

    struct PluginHostWorkerHandle
    {
        virtual ~PluginHostWorkerHandle() = default;
        virtual bool isFinished() const = 0;   // true once the coordinator dropped the connection
    };

    /** Worker side: if `commandLine` marks this process as a plugin-host worker, start it
        (loading the plugin via `loader`) and return a handle to keep alive; else nullptr. */
    std::shared_ptr<PluginHostWorkerHandle> startPluginHostWorkerIfRequested (
        const juce::String& commandLine, PluginLoader loader);

    //==============================================================================
    class OutOfProcessPluginInstance : public juce::AudioPluginInstance
    {
    public:
        /** Spawns the child worker and queries the plugin's metadata (parameters, MIDI flags)
            synchronously, so TE sees the real parameter list when it wraps this instance.
            If the worker can't launch, the instance stays valid but silent + parameterless. */
        OutOfProcessPluginInstance (const juce::PluginDescription& desc, int numChannels);
        ~OutOfProcessPluginInstance() override;

        // ---- AudioProcessor ----
        const juce::String getName() const override { return desc.name; }
        void prepareToPlay (double sampleRate, int blockSize) override;
        void releaseResources() override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return childAcceptsMidi; }     // E5.4
        bool producesMidi() const override { return childProducesMidi; }   // E5.4

        /** E5.5: die ECHTE Plugin-GUI lebt als child-owned Fenster im Worker-Prozess
            (crash-sicher; ein GUI-Hänger friert die DAW nicht ein). Der Host-Editor
            ist ein schmales Panel mit "GUI öffnen" und öffnet das Child-Fenster
            direkt mit; sein Schließen schließt auch das Child-Fenster. */
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return childHasEditor; }

        /** E5.5: Child-GUI-Fenster anzeigen/schließen (fire-and-forget über den
            Coordinator-Kanal; ohne lebenden Worker ein No-op). */
        void showChildGui();
        void hideChildGui();
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override;            // E5.4
        void setStateInformation (const void*, int) override;              // E5.4

        /** Message-thread load result, distinct from subsequent audio preparation. */
        bool isChildLoaded() const noexcept { return childLoaded; }

        /** True once the child process has loaded the plugin and is pumping audio. */
        bool isChildConnected() const noexcept { return childReady.load(); }

        /** Parameter proxies call this (any thread, lock-free) — drained into the ring's
            param region on the next processBlock. */
        void queueParamChange (uint32_t index, float value) noexcept;

        /** NUR für den Cross-Process-Smoke-Test: lässt den Worker hart crashen
            (_exit), um die Crash-Restart-Policy end-to-end zu verifizieren. */
        void crashChildForTest();

    private:
        void teardown();
        bool ensureCoordinator();   // (re)spawn worker + describe; builds params on first success
        void dropCoordinator();     // intentionaler Reset — onLost darf dabei NICHT feuern
        void handleWorkerDied();    // E5.6: Crash-Restart (Message-Thread)
        bool restartBudgetAllows(); // max 3 Respawns pro Minute, dann dauerhaft still

        struct Impl;                                   // hides the ChildProcessCoordinator
        juce::PluginDescription          desc;
        int                              numCh;
        std::unique_ptr<SharedAudioRing> ring;
        std::unique_ptr<Impl>            impl;
        juce::String                     ringName;
        std::atomic<bool>                childReady { false };
        uint32_t                         timeoutMicros { 8000 };
        bool                             childAcceptsMidi  { false };
        bool                             childProducesMidi { false };
        bool                             childHasEditor    { false };   // E5.5
        bool                             paramsBuilt { false };
        bool                             childLoaded { false };

        // E5.6: Crash-Restart-Policy — Respawn nach Worker-Tod, budgetiert.
        std::shared_ptr<std::atomic<bool>> aliveGuard { std::make_shared<std::atomic<bool>> (true) };
        double                           lastPreparedRate  { 0.0 };
        int                              lastPreparedBlock { 0 };
        int                              restartCount { 0 };
        juce::int64                      restartWindowStartMs { 0 };
        juce::MemoryBlock                lastKnownState;   // re-applied after a respawn

        // Lock-free param-change queue: proxies write, processBlock drains (audio thread).
        static constexpr int             kParamFifoSize = 2048;
        juce::AbstractFifo               paramFifo { kParamFifoSize };
        OopParamEvent                    paramSlots[kParamFifoSize] = {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessPluginInstance)
    };

    //==============================================================================
    /**
        OutOfProcessPluginFormat — a hosting-only AudioPluginFormat registered in the TE engine's
        format manager. It never scans (descriptions are injected with pluginFormatName ==
        kOopFormatName by the app when the flag is on); createPluginInstance returns an
        OutOfProcessPluginInstance so TE hosts the plugin in a child process transparently.
    */
    class OutOfProcessPluginFormat : public juce::AudioPluginFormat
    {
    public:
        OutOfProcessPluginFormat() = default;

        juce::String getName() const override { return kOopFormatName; }
        bool canScanForPlugins() const override { return false; }
        bool isTrivialToScan() const override { return true; }
        juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
        bool fileMightContainThisPluginType (const juce::String& identifier) override
        {
            // JUCE also calls this when resolving an explicit hosting description.
            // Scanning remains disabled by canScanForPlugins()/findAllTypesForFile().
            // The format name gates dispatch; AU identifiers need not be file paths.
            return identifier.isNotEmpty();
        }
        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
        bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
        bool doesPluginStillExist (const juce::PluginDescription& d) override
        {
            // AU-Identifier sind keine Pfade ("AudioUnit:Effects/…") — nicht
            // verifizierbar heißt hier: als existent annehmen (E5.6, 3rd-party).
            if (! juce::File::isAbsolutePath (d.fileOrIdentifier))
                return true;
            return juce::File (d.fileOrIdentifier).exists();
        }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const noexcept override { return false; }

        void createPluginInstance (const juce::PluginDescription& desc, double initialSampleRate,
                                   int initialBufferSize, PluginCreationCallback callback) override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutOfProcessPluginFormat)
    };
}
