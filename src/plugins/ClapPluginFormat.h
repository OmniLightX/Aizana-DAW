#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/clap.h>

/**
 * ClapPluginFormat — JUCE AudioPluginFormat subclass for CLAP plugin hosting.
 *
 * Uses the direct CLAP C API (clap/clap.h) rather than the alpha
 * juce_clap_hosting community module (see RESEARCH.md Pitfall 1).
 *
 * Phase 1 scope: Basic audio/MIDI I/O only.
 * Deferred: polyphonic modulation, note expressions (RESEARCH.md Open Question 1).
 *
 * Thread safety note (Pitfall 7 from RESEARCH.md):
 *   - Plugin INSTANTIATION may happen on any thread.
 *   - Plugin WINDOW creation MUST be on the message thread:
 *     juce::MessageManager::callAsync([this] { createEditorIfNeeded(); });
 *   - processBlock() is called on the audio thread — never touch the message
 *     thread or allocate memory from processBlock().
 */
class ClapPluginFormat : public juce::AudioPluginFormat
{
public:
    ClapPluginFormat();
    ~ClapPluginFormat() override;

    // ---------------------------------------------------------------------------
    // AudioPluginFormat interface
    // ---------------------------------------------------------------------------

    juce::String getName() const override { return "CLAP"; }

    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::FileSearchPath getDefaultLocationsToSearch() override;

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;

    bool pluginNeedsRescanning (const juce::PluginDescription& desc) override;

    bool doesPluginStillExist (const juce::PluginDescription& desc) override;

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& path,
                                             bool recursive,
                                             bool allowPluginsWhichRequireAsynchronousInstantiation) override;

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String& fileOrIdentifier) override;

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const noexcept override
    {
        return false;
    }

    // Synchronous wrapper around the async createPluginInstance — used in tests.
    // Blocks the calling thread until the callback fires.
    void createPluginInstance (const juce::PluginDescription& desc,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override;

protected:
    // Internal async creation — delegates to createPluginInstance above.
    // Kept for completeness; the JUCE base class calls this form on host side.

private:
    // ---------------------------------------------------------------------------
    // Private types
    // ---------------------------------------------------------------------------

    /** Holds a loaded .clap shared library and its entry point. */
    struct ClapLibrary
    {
        void*                      handle  { nullptr };
        const clap_plugin_entry_t* entry   { nullptr };
        juce::String               path;

        ClapLibrary() = default;
        ~ClapLibrary();

        // Non-copyable
        ClapLibrary (const ClapLibrary&)            = delete;
        ClapLibrary& operator= (const ClapLibrary&) = delete;
    };

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    /** dlopen / LoadLibrary wrapper. Returns nullptr on failure. */
    static void* openSharedLibrary (const juce::String& path);

    /** dlclose / FreeLibrary wrapper. */
    static void closeSharedLibrary (void* handle);

    /** Resolve a named symbol from the library. */
    static void* getSymbol (void* handle, const char* name);

    /** Fill a PluginDescription from a clap_plugin_descriptor_t. */
    static juce::PluginDescription makeDescription (const clap_plugin_descriptor_t& desc,
                                                    const juce::String& filePath);

    friend class ClapAudioPluginInstance;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginFormat)
};

// ---------------------------------------------------------------------------
// ClapAudioPluginInstance — AudioPluginInstance wrapper around a clap_plugin_t*.
// Declared here for visibility from tests; defined in ClapPluginFormat.cpp.
// ---------------------------------------------------------------------------
class ClapAudioPluginInstance : public juce::AudioPluginInstance
{
public:
    ClapAudioPluginInstance (const clap_plugin_t*      plugin,
                             const clap_plugin_entry_t* entry,
                             void*                       libraryHandle,
                             const juce::PluginDescription& description);

    ~ClapAudioPluginInstance() override;

    // ---------------------------------------------------------------------------
    // AudioProcessor interface
    // ---------------------------------------------------------------------------

    const juce::String getName() const override { return desc.name; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    // ---------------------------------------------------------------------------
    // AudioPluginInstance interface
    // ---------------------------------------------------------------------------

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    const clap_plugin_t*       plugin        { nullptr };
    const clap_plugin_entry_t* entry         { nullptr };
    void*                      libraryHandle { nullptr };
    juce::PluginDescription    desc;

    bool activated { false };

    /** Map clap_plugin_params descriptors to AudioProcessorParameter objects. */
    void buildParameterList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapAudioPluginInstance)
};
