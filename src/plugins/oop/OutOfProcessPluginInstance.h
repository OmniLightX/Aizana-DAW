#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SharedAudioRing.h"

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
namespace cloudes
{
    // Command-line marker that launches a process in plugin-host worker mode (like the scanner).
    inline constexpr const char* kPluginHostProcessUID = "cloudes_plugin_host";

    using PluginLoader = std::function<std::unique_ptr<juce::AudioProcessor> (
        const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error)>;

    // Descriptions whose pluginFormatName equals this route through OutOfProcessPluginFormat.
    inline constexpr const char* kOopFormatName = "Cloudes OOP";

    /** True when out-of-process plugin hosting is enabled (env CLOUDES_OOP_PLUGINS=1).
        Default OFF — in-process hosting stays the shipping path until E5 is complete. */
    bool oopHostingEnabled();

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
        OutOfProcessPluginInstance (const juce::PluginDescription& desc, int numChannels);
        ~OutOfProcessPluginInstance() override;

        // ---- AudioProcessor ----
        const juce::String getName() const override { return desc.name; }
        void prepareToPlay (double sampleRate, int blockSize) override;
        void releaseResources() override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }   // MIDI across the boundary = E5.4
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }   // GUI = E5.5
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        /** True once the child process has loaded the plugin and is pumping audio. */
        bool isChildConnected() const noexcept { return childReady.load(); }

    private:
        void teardown();

        struct Impl;                                   // hides the ChildProcessCoordinator
        juce::PluginDescription          desc;
        int                              numCh;
        std::unique_ptr<SharedAudioRing> ring;
        std::unique_ptr<Impl>            impl;
        juce::String                     ringName;
        std::atomic<bool>                childReady { false };
        uint32_t                         timeoutMicros { 8000 };

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
        bool fileMightContainThisPluginType (const juce::String&) override { return false; }
        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
        bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
        bool doesPluginStillExist (const juce::PluginDescription& d) override
        {
            return juce::File::isAbsolutePath (d.fileOrIdentifier) && juce::File (d.fileOrIdentifier).exists();
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
