#include "ClapPluginFormat.h"

#include <atomic>

#if JUCE_MAC || JUCE_LINUX
  #include <dlfcn.h>
#elif JUCE_WINDOWS
  #include <windows.h>
#endif

// Max parameter-change events drained from the FIFO into a single process block.
// Far more than a human can generate per ~1.5 ms block; surplus stays queued.
static constexpr int kMaxParamEventsPerBlock = 128;

// ===========================================================================
// Stub host callbacks (C-compatible, CLAP_ABI calling convention)
// Phase 1: minimal stub sufficient for basic audio/MIDI I/O hosting.
// ===========================================================================

static const void* CLAP_ABI stubHostGetExtension (const clap_host_t*, const char*) { return nullptr; }
static void CLAP_ABI stubHostRequestRestart  (const clap_host_t*) {}
static void CLAP_ABI stubHostRequestProcess  (const clap_host_t*) {}
static void CLAP_ABI stubHostRequestCallback (const clap_host_t*) {}

// Stub output event list — silently discards
static bool CLAP_ABI discardOutputTryPush (const clap_output_events_t*, const clap_event_header_t*) { return true; }

// ---------------------------------------------------------------------------
// Audio-thread input event list backed by a stack array of param-value events.
// `ctx` points to a ClapInputEventList that lives on the process() stack.
// ---------------------------------------------------------------------------
struct ClapInputEventList
{
    const clap_event_param_value_t* events { nullptr };
    uint32_t                        count  { 0 };
};

static uint32_t CLAP_ABI inputEventsSize (const clap_input_events_t* list)
{
    return static_cast<const ClapInputEventList*> (list->ctx)->count;
}

static const clap_event_header_t* CLAP_ABI inputEventsGet (const clap_input_events_t* list, uint32_t index)
{
    auto* l = static_cast<const ClapInputEventList*> (list->ctx);
    if (index >= l->count)
        return nullptr;
    return &l->events[index].header;
}

// ===========================================================================
// Static stub host definition — shared across all ClapAudioPluginInstances.
// This object must have static storage duration so its address is stable.
// ===========================================================================

static const clap_host_t kStubHost {
    CLAP_VERSION,
    nullptr,               // host_data
    "Cloudes",             // name
    "Cloudes",             // vendor
    "https://cloudes.app", // url
    "0.1.0",               // version
    stubHostGetExtension,
    stubHostRequestRestart,
    stubHostRequestProcess,
    stubHostRequestCallback
};

// ===========================================================================
// ClapLibrary destructor
// ===========================================================================

ClapPluginFormat::ClapLibrary::~ClapLibrary()
{
    if (entry != nullptr)
    {
        entry->deinit();
        entry = nullptr;
    }
    if (handle != nullptr)
    {
        ClapPluginFormat::closeSharedLibrary (handle);
        handle = nullptr;
    }
}

// ===========================================================================
// ClapPluginFormat
// ===========================================================================

ClapPluginFormat::ClapPluginFormat()  = default;
ClapPluginFormat::~ClapPluginFormat() = default;

// ---------------------------------------------------------------------------
// File type identification
// ---------------------------------------------------------------------------

bool ClapPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.endsWithIgnoreCase (".clap");
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    return juce::File (fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).getLastModificationTime() > desc.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist (const juce::PluginDescription& desc)
{
    return juce::File (desc.fileOrIdentifier).exists();
}

// ---------------------------------------------------------------------------
// Search paths
// ---------------------------------------------------------------------------

juce::StringArray ClapPluginFormat::searchPathsForPlugins (
    const juce::FileSearchPath& searchPath,
    bool recursive,
    bool /*allowPluginsWhichRequireAsynchronousInstantiation*/)
{
    juce::StringArray results;

    for (int i = 0; i < searchPath.getNumPaths(); ++i)
    {
        const auto dir = searchPath[i];
        if (! dir.exists())
            continue;

        auto clapFiles = dir.findChildFiles (
            juce::File::findFiles,
            recursive,
            "*.clap");

        for (const auto& f : clapFiles)
            results.add (f.getFullPathName());
    }

    return results;
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void ClapPluginFormat::findAllTypesForFile (
    juce::OwnedArray<juce::PluginDescription>& results,
    const juce::String& fileOrIdentifier)
{
    if (! fileMightContainThisPluginType (fileOrIdentifier))
        return;

    const juce::File file (fileOrIdentifier);
    if (! file.exists())
        return;

    void* handle = openSharedLibrary (fileOrIdentifier);
    if (handle == nullptr)
    {
        juce::Logger::writeToLog ("ClapPluginFormat: could not open " + fileOrIdentifier);
        return;
    }

    auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (
        getSymbol (handle, "clap_entry"));

    if (entry == nullptr || entry->init == nullptr || entry->get_factory == nullptr)
    {
        juce::Logger::writeToLog ("ClapPluginFormat: no valid clap_entry in " + fileOrIdentifier);
        closeSharedLibrary (handle);
        return;
    }

    if (! entry->init (fileOrIdentifier.toRawUTF8()))
    {
        juce::Logger::writeToLog ("ClapPluginFormat: clap_entry.init() failed for " + fileOrIdentifier);
        closeSharedLibrary (handle);
        return;
    }

    auto* factory = reinterpret_cast<const clap_plugin_factory_t*> (
        entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

    if (factory == nullptr)
    {
        juce::Logger::writeToLog ("ClapPluginFormat: no clap_plugin_factory in " + fileOrIdentifier);
        entry->deinit();
        closeSharedLibrary (handle);
        return;
    }

    const uint32_t count = factory->get_plugin_count (factory);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* plugDesc = factory->get_plugin_descriptor (factory, i);
        if (plugDesc == nullptr)
            continue;

        auto desc = std::make_unique<juce::PluginDescription> (
            makeDescription (*plugDesc, fileOrIdentifier));
        results.add (desc.release());
    }

    // Always deinit after scanning — factory is only needed during scan.
    entry->deinit();
    closeSharedLibrary (handle);
}

// ---------------------------------------------------------------------------
// Instantiation
// ---------------------------------------------------------------------------

void ClapPluginFormat::createPluginInstance (
    const juce::PluginDescription& desc,
    double initialSampleRate,
    int initialBufferSize,
    PluginCreationCallback callback)
{
    juce::ignoreUnused (initialSampleRate, initialBufferSize);

    const juce::String path = desc.fileOrIdentifier;
    const juce::File file (path);

    if (! file.exists())
    {
        callback (nullptr, "Plugin file not found: " + path);
        return;
    }

    void* handle = openSharedLibrary (path);
    if (handle == nullptr)
    {
        callback (nullptr, "Could not load CLAP library: " + path);
        return;
    }

    auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (
        getSymbol (handle, "clap_entry"));

    if (entry == nullptr || entry->init == nullptr || entry->get_factory == nullptr)
    {
        closeSharedLibrary (handle);
        callback (nullptr, "Invalid clap_entry in: " + path);
        return;
    }

    if (! entry->init (path.toRawUTF8()))
    {
        closeSharedLibrary (handle);
        callback (nullptr, "clap_entry.init() failed for: " + path);
        return;
    }

    auto* factory = reinterpret_cast<const clap_plugin_factory_t*> (
        entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

    if (factory == nullptr)
    {
        entry->deinit();
        closeSharedLibrary (handle);
        callback (nullptr, "No clap_plugin_factory in: " + path);
        return;
    }

    // Find the matching plugin by the ID stored in desc.version
    const uint32_t count = factory->get_plugin_count (factory);
    const clap_plugin_t* plugin = nullptr;

    for (uint32_t i = 0; i < count && plugin == nullptr; ++i)
    {
        const auto* plugDesc = factory->get_plugin_descriptor (factory, i);
        if (plugDesc == nullptr)
            continue;

        if (desc.version == juce::String::fromUTF8 (plugDesc->id))
        {
            plugin = factory->create_plugin (factory, &kStubHost, plugDesc->id);
            if (plugin != nullptr)
            {
                if (plugin->init == nullptr || ! plugin->init (plugin))
                {
                    if (plugin->destroy != nullptr)
                        plugin->destroy (plugin);
                    plugin = nullptr;
                }
            }
        }
    }

    if (plugin == nullptr)
    {
        entry->deinit();
        closeSharedLibrary (handle);
        callback (nullptr, "Could not create CLAP plugin instance from: " + path);
        return;
    }

    auto instance = std::make_unique<ClapAudioPluginInstance> (plugin, entry, handle, desc);
    callback (std::move (instance), {});
}

// ---------------------------------------------------------------------------
// Private helpers — shared library loading
// ---------------------------------------------------------------------------

void* ClapPluginFormat::openSharedLibrary (const juce::String& path)
{
#if JUCE_MAC || JUCE_LINUX
    void* handle = ::dlopen (path.toRawUTF8(), RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr)
        juce::Logger::writeToLog (juce::String ("ClapPluginFormat dlopen failed: ") + juce::String (::dlerror()));
    return handle;
#elif JUCE_WINDOWS
    HMODULE mod = ::LoadLibraryW (path.toWideCharPointer());
    return reinterpret_cast<void*> (mod);
#else
    juce::ignoreUnused (path);
    return nullptr;
#endif
}

void ClapPluginFormat::closeSharedLibrary (void* handle)
{
    if (handle == nullptr)
        return;
#if JUCE_MAC || JUCE_LINUX
    ::dlclose (handle);
#elif JUCE_WINDOWS
    ::FreeLibrary (reinterpret_cast<HMODULE> (handle));
#endif
}

void* ClapPluginFormat::getSymbol (void* handle, const char* name)
{
    if (handle == nullptr)
        return nullptr;
#if JUCE_MAC || JUCE_LINUX
    return ::dlsym (handle, name);
#elif JUCE_WINDOWS
    return reinterpret_cast<void*> (
        ::GetProcAddress (reinterpret_cast<HMODULE> (handle), name));
#else
    juce::ignoreUnused (name);
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Public interface — default search locations
// ---------------------------------------------------------------------------

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;
#if JUCE_MAC
    paths.add (juce::File ("~/Library/Audio/Plug-Ins/CLAP"));
    paths.add (juce::File ("/Library/Audio/Plug-Ins/CLAP"));
#elif JUCE_WINDOWS
    const auto commonFiles = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
    paths.add (commonFiles.getChildFile ("CLAP"));
    const auto userFiles = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    paths.add (userFiles.getChildFile ("CLAP"));
#elif JUCE_LINUX
    paths.add (juce::File ("~/.clap"));
    paths.add (juce::File ("/usr/lib/clap"));
    paths.add (juce::File ("/usr/local/lib/clap"));
#endif
    return paths;
}

// ---------------------------------------------------------------------------
// Private helpers — PluginDescription construction
// ---------------------------------------------------------------------------

juce::PluginDescription ClapPluginFormat::makeDescription (
    const clap_plugin_descriptor_t& clapDesc,
    const juce::String& filePath)
{
    juce::PluginDescription d;
    d.pluginFormatName  = "CLAP";
    d.name              = juce::String::fromUTF8 (clapDesc.name        ? clapDesc.name        : "");
    d.manufacturerName  = juce::String::fromUTF8 (clapDesc.vendor      ? clapDesc.vendor      : "");
    // Store the CLAP plugin ID in the `version` field so createPluginInstance can match it.
    d.version           = juce::String::fromUTF8 (clapDesc.id          ? clapDesc.id          : "");
    d.descriptiveName   = juce::String::fromUTF8 (clapDesc.description ? clapDesc.description : "");
    d.fileOrIdentifier  = filePath;
    d.lastFileModTime   = juce::File (filePath).getLastModificationTime();
    // Conservative default for Phase 1 — will be updated from features[] in a later phase
    d.isInstrument      = false;
    d.numInputChannels  = 2;
    d.numOutputChannels = 2;
    return d;
}

// ===========================================================================
// ClapParameter — JUCE AudioProcessorParameter wrapping a CLAP param
// ===========================================================================

class ClapParameter : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapAudioPluginInstance&    owner,
                   const clap_plugin_t*        plugin,
                   const clap_plugin_params_t* paramsExt,
                   clap_id                     paramId,
                   uint32_t                    paramIndex,
                   const juce::String&         name,
                   double                      minValue,
                   double                      maxValue,
                   float                       defaultValue)
        : owner      (owner)
        , plugin     (plugin)
        , paramsExt  (paramsExt)
        , paramId    (paramId)
        , paramIndex (paramIndex)
        , paramName  (name)
        , rangeMin   (minValue)
        , rangeSize  (maxValue - minValue)
        , defaultVal (defaultValue)
        , currentNorm (defaultValue)
    {}

    // The host is the source of truth for the value: we return the last value the
    // host set (or the default), not a read-back from the plugin. This keeps the UI
    // thumb stable and instant — the plugin converges on the next process() block.
    float getValue() const override { return currentNorm.load (std::memory_order_relaxed); }

    void setValue (float newValue) override
    {
        newValue = juce::jlimit (0.0f, 1.0f, newValue);
        currentNorm.store (newValue, std::memory_order_relaxed);
        owner.enqueueParamChange (paramId, rangeMin + (double) newValue * rangeSize);
    }

    float getDefaultValue() const override { return defaultVal; }

    juce::String getName (int maximumStringLength) const override
    {
        return paramName.substring (0, maximumStringLength);
    }

    juce::String getLabel() const override { return {}; }

    juce::String getParameterID() const override
    {
        return juce::String (static_cast<juce::int64> (paramId));
    }

    // Format the normalised value using the plugin's own value_to_text (eg "2.3 kHz",
    // "-6 dB"). [main-thread] per ext/params.h — getText is only called from the UI.
    juce::String getText (float normValue, int maximumStringLength) const override
    {
        if (paramsExt != nullptr && plugin != nullptr && paramsExt->value_to_text != nullptr)
        {
            const double raw = rangeMin + (double) juce::jlimit (0.0f, 1.0f, normValue) * rangeSize;
            char buffer[128] = {};
            if (paramsExt->value_to_text (plugin, paramId, raw, buffer, sizeof (buffer)))
            {
                juce::String s = juce::String::fromUTF8 (buffer);
                return maximumStringLength > 0 ? s.substring (0, maximumStringLength) : s;
            }
        }
        return juce::AudioProcessorParameter::getText (normValue, maximumStringLength);
    }

    float getValueForText (const juce::String& text) const override
    {
        if (paramsExt != nullptr && plugin != nullptr && paramsExt->text_to_value != nullptr)
        {
            double raw = 0.0;
            if (paramsExt->text_to_value (plugin, paramId, text.toRawUTF8(), &raw))
                return rangeSize > 0.0 ? (float) juce::jlimit (0.0, 1.0, (raw - rangeMin) / rangeSize)
                                       : 0.0f;
        }
        // Fallback: interpret the text as a raw value in [min, max] and normalise.
        return rangeSize > 0.0
                 ? (float) juce::jlimit (0.0, 1.0, ((double) text.getFloatValue() - rangeMin) / rangeSize)
                 : 0.0f;
    }

private:
    ClapAudioPluginInstance&    owner;
    const clap_plugin_t*        plugin     { nullptr };
    const clap_plugin_params_t* paramsExt  { nullptr };
    clap_id                     paramId    { CLAP_INVALID_ID };
    uint32_t                    paramIndex { 0 };
    juce::String                paramName;
    double                      rangeMin   { 0.0 };
    double                      rangeSize  { 1.0 };
    float                       defaultVal { 0.0f };
    std::atomic<float>          currentNorm { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapParameter)
};

// ===========================================================================
// ClapAudioPluginInstance
// ===========================================================================

ClapAudioPluginInstance::ClapAudioPluginInstance (
    const clap_plugin_t*       plugin,
    const clap_plugin_entry_t* entry,
    void*                      libraryHandle,
    const juce::PluginDescription& description)
    : plugin        (plugin)
    , entry         (entry)
    , libraryHandle (libraryHandle)
    , desc          (description)
{
    setPlayConfigDetails (desc.numInputChannels,
                          desc.numOutputChannels,
                          44100.0,
                          512);
    // Pre-allocate the param-change queue storage on the message thread (never on audio).
    paramChangeStorage.resize (kParamFifoCapacity);
    buildParameterList();
}

ClapAudioPluginInstance::~ClapAudioPluginInstance()
{
    if (activated && plugin != nullptr)
    {
        if (plugin->stop_processing != nullptr)
            plugin->stop_processing (plugin);
        if (plugin->deactivate != nullptr)
            plugin->deactivate (plugin);
    }
    if (plugin != nullptr && plugin->destroy != nullptr)
        plugin->destroy (plugin);

    if (entry != nullptr)
        entry->deinit();

    ClapPluginFormat::closeSharedLibrary (libraryHandle);
}

void ClapAudioPluginInstance::buildParameterList()
{
    if (plugin == nullptr)
        return;

    auto* paramsExt = reinterpret_cast<const clap_plugin_params_t*> (
        plugin->get_extension (plugin, CLAP_EXT_PARAMS));

    if (paramsExt == nullptr)
        return;

    const uint32_t count = paramsExt->count (plugin);
    for (uint32_t i = 0; i < count; ++i)
    {
        clap_param_info_t info{};
        if (! paramsExt->get_info (plugin, i, &info))
            continue;

        // Normalise default from [min, max] to [0, 1]
        float normDefault = 0.0f;
        const double range = info.max_value - info.min_value;
        if (range > 0.0)
            normDefault = static_cast<float> ((info.default_value - info.min_value) / range);

        addHostedParameter (std::make_unique<ClapParameter> (*this, plugin, paramsExt, info.id, i,
                                                              juce::String::fromUTF8 (info.name),
                                                              info.min_value, info.max_value,
                                                              normDefault));
    }
}

void ClapAudioPluginInstance::enqueueParamChange (clap_id id, double rawValue) noexcept
{
    // Message thread (producer). Lock-free single-slot write; if the queue is full
    // (audio thread starved) the change is dropped — the next move supersedes it.
    int start1, size1, start2, size2;
    paramChangeFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0)
        paramChangeStorage[(size_t) start1] = { id, rawValue };
    paramChangeFifo.finishedWrite (size1);
}

void ClapAudioPluginInstance::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (plugin == nullptr)
        return;

    if (activated)
    {
        if (plugin->stop_processing != nullptr) plugin->stop_processing (plugin);
        if (plugin->deactivate      != nullptr) plugin->deactivate (plugin);
        activated = false;
    }

    setPlayConfigDetails (desc.numInputChannels,
                          desc.numOutputChannels,
                          sampleRate,
                          maximumExpectedSamplesPerBlock);

    if (plugin->activate != nullptr &&
        plugin->activate (plugin,
                          sampleRate,
                          static_cast<uint32_t> (maximumExpectedSamplesPerBlock),
                          static_cast<uint32_t> (maximumExpectedSamplesPerBlock)))
    {
        if (plugin->start_processing != nullptr)
            plugin->start_processing (plugin);
        activated = true;
    }
    else
    {
        juce::Logger::writeToLog ("ClapAudioPluginInstance: activate() returned false");
    }
}

void ClapAudioPluginInstance::releaseResources()
{
    if (plugin == nullptr || ! activated)
        return;

    if (plugin->stop_processing != nullptr) plugin->stop_processing (plugin);
    if (plugin->deactivate      != nullptr) plugin->deactivate (plugin);
    activated = false;
}

void ClapAudioPluginInstance::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    // CRITICAL (Pitfall 7 from RESEARCH.md): audio thread.
    // No memory allocations, no locks, no I/O, no message thread calls.

    juce::ignoreUnused (midiMessages);

    if (plugin == nullptr || ! activated || plugin->process == nullptr)
    {
        buffer.clear();
        return;
    }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int clapChans   = std::min (numChannels, 2);

    // Build channel pointer arrays for CLAP audio buffers
    float* channelPtrs[2] = { nullptr, nullptr };
    for (int ch = 0; ch < clapChans; ++ch)
        channelPtrs[ch] = buffer.getWritePointer (ch);

    // CLAP uses the same buffer for in-place processing (input and output share pointers)
    clap_audio_buffer_t audioBuffer {
        channelPtrs,           // data32
        nullptr,               // data64
        static_cast<uint32_t> (clapChans),
        0,                     // latency
        0                      // constant_mask
    };

    // Drain pending host→plugin parameter changes into CLAP param-value events.
    // Stack-allocated, lock-free SPSC read — audio-thread safe (no alloc, no lock).
    clap_event_param_value_t paramEvents[kMaxParamEventsPerBlock];
    uint32_t numParamEvents = 0;
    {
        const int toRead = juce::jmin (paramChangeFifo.getNumReady(), kMaxParamEventsPerBlock);
        int start1, size1, start2, size2;
        paramChangeFifo.prepareToRead (toRead, start1, size1, start2, size2);

        const auto appendRange = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& c = paramChangeStorage[(size_t) (start + i)];
                auto& e = paramEvents[numParamEvents++];
                e.header.size     = sizeof (clap_event_param_value_t);
                e.header.time     = 0;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type     = CLAP_EVENT_PARAM_VALUE;
                e.header.flags    = 0;
                e.param_id        = c.id;
                e.cookie          = nullptr;
                e.note_id         = -1;
                e.port_index      = -1;
                e.channel         = -1;
                e.key             = -1;
                e.value           = c.value;
            }
        };
        appendRange (start1, size1);
        appendRange (start2, size2);
        paramChangeFifo.finishedRead (size1 + size2);
    }

    ClapInputEventList inEventList { paramEvents, numParamEvents };
    const clap_input_events_t inputEvents {
        &inEventList,
        inputEventsSize,
        inputEventsGet
    };

    static const clap_output_events_t kDiscardOutputEvents {
        nullptr,
        discardOutputTryPush
    };

    clap_process_t process {
        -1LL,                                        // steady_time — unknown
        static_cast<uint32_t> (numSamples),          // frames_count
        nullptr,                                     // transport
        &audioBuffer,                                // audio_inputs (in-place)
        &audioBuffer,                                // audio_outputs (in-place)
        1,                                           // audio_inputs_count
        1,                                           // audio_outputs_count
        &inputEvents,
        &kDiscardOutputEvents
    };

    plugin->process (plugin, &process);
}

bool ClapAudioPluginInstance::hasEditor() const
{
    if (plugin == nullptr)
        return false;

    auto* guiExt = reinterpret_cast<const clap_plugin_gui_t*> (
        plugin->get_extension (plugin, CLAP_EXT_GUI));
    return guiExt != nullptr;
}

juce::AudioProcessorEditor* ClapAudioPluginInstance::createEditor()
{
    // CRITICAL (Pitfall 7 from RESEARCH.md):
    // Plugin window creation MUST be on the message thread.
    // Callers must use: juce::MessageManager::callAsync([this] { createEditorIfNeeded(); });
    //
    // Phase 1: CLAP GUI hosting (clap_plugin_gui_t / clap_window_t) is deferred.
    // Platform-specific window embedding (NSView on macOS, HWND on Windows) is a
    // Phase 2+ concern. Returning nullptr here is correct for Phase 1.
    return nullptr;
}

bool ClapAudioPluginInstance::acceptsMidi() const
{
    if (plugin == nullptr) return false;

    auto* notePortsExt = reinterpret_cast<const clap_plugin_note_ports_t*> (
        plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));

    if (notePortsExt == nullptr) return false;
    return notePortsExt->count (plugin, true /* is_input */) > 0;
}

bool ClapAudioPluginInstance::producesMidi() const
{
    if (plugin == nullptr) return false;

    auto* notePortsExt = reinterpret_cast<const clap_plugin_note_ports_t*> (
        plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));

    if (notePortsExt == nullptr) return false;
    return notePortsExt->count (plugin, false /* is_input = output */) > 0;
}
