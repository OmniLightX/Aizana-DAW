#include "BuiltInPluginFormat.h"
#include "fx/EQProcessor.h"
#include "fx/CompressorProcessor.h"
#include "fx/ReverbProcessor.h"
#include "fx/LimiterProcessor.h"
#include "fx/DeEsserProcessor.h"
#include "fx/DelayProcessor.h"
#include "fx/GateProcessor.h"
#include "fx/SaturatorProcessor.h"

// ---------------------------------------------------------------------------
// Static description helpers
// ---------------------------------------------------------------------------

namespace
{
    struct BuiltInEntry
    {
        const char* name;
        const char* category;
        const char* identifier;
    };

    // Single source of truth for all built-in FX — descriptions, scan results
    // and instantiation all derive from this table.
    constexpr BuiltInEntry kBuiltIns[] =
    {
        { "Aizana EQ",         "EQ",         "aizana.eq" },
        { "Aizana Compressor", "Dynamics",   "aizana.compressor" },
        { "Aizana Reverb",     "Reverb",     "aizana.reverb" },
        { "Aizana Limiter",    "Dynamics",   "aizana.limiter" },
        { "Aizana De-Esser",   "Dynamics",   "aizana.deesser" },
        { "Aizana Delay",      "Delay",      "aizana.delay" },
        { "Aizana Gate",       "Dynamics",   "aizana.gate" },
        { "Aizana Saturator",  "Distortion", "aizana.saturator" },
    };
}

static juce::PluginDescription makeDesc (const juce::String& name,
                                          const juce::String& category,
                                          const juce::String& identifier)
{
    juce::PluginDescription desc;
    desc.name             = name;
    desc.pluginFormatName = "Built-in";
    desc.manufacturerName = "Aizana";
    desc.category         = category;
    desc.fileOrIdentifier = identifier;
    desc.uniqueId         = identifier.hashCode();
    desc.isInstrument     = false;
    desc.numInputChannels = 2;
    desc.numOutputChannels = 2;
    desc.version          = "1.0";
    return desc;
}

// ---------------------------------------------------------------------------
// BuiltInPluginFormat
// ---------------------------------------------------------------------------

void BuiltInPluginFormat::getBuiltInDescriptions (juce::OwnedArray<juce::PluginDescription>& results)
{
    for (const auto& e : kBuiltIns)
        results.add (new juce::PluginDescription (makeDesc (e.name, e.category, e.identifier)));
}

juce::PluginDescription BuiltInPluginFormat::getDescriptionFor (const juce::String& fxName)
{
    for (const auto& e : kBuiltIns)
        if (fxName == e.name)
            return makeDesc (e.name, e.category, e.identifier);

    jassertfalse;  // Unknown built-in FX name
    return {};
}

// ---------------------------------------------------------------------------
// AudioPluginFormat interface
// ---------------------------------------------------------------------------

void BuiltInPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                                const juce::String& fileOrIdentifier)
{
    for (const auto& e : kBuiltIns)
        if (fileOrIdentifier == e.identifier)
            results.add (new juce::PluginDescription (makeDesc (e.name, e.category, e.identifier)));
}

bool BuiltInPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.startsWith ("aizana.");
}

juce::StringArray BuiltInPluginFormat::searchPathsForPlugins (const juce::FileSearchPath&, bool, bool)
{
    juce::StringArray paths;
    for (const auto& e : kBuiltIns)
        paths.add (e.identifier);
    return paths;
}

void BuiltInPluginFormat::createPluginInstance (const juce::PluginDescription& desc,
                                                 double initialSampleRate,
                                                 int initialBufferSize,
                                                 PluginCreationCallback callback)
{
    std::unique_ptr<juce::AudioPluginInstance> instance;

    if (desc.fileOrIdentifier == "aizana.eq")
        instance = std::make_unique<EQProcessor>();
    else if (desc.fileOrIdentifier == "aizana.compressor")
        instance = std::make_unique<CompressorProcessor>();
    else if (desc.fileOrIdentifier == "aizana.reverb")
        instance = std::make_unique<ReverbProcessor>();
    else if (desc.fileOrIdentifier == "aizana.limiter")
        instance = std::make_unique<LimiterProcessor>();
    else if (desc.fileOrIdentifier == "aizana.deesser")
        instance = std::make_unique<DeEsserProcessor>();
    else if (desc.fileOrIdentifier == "aizana.delay")
        instance = std::make_unique<DelayProcessor>();
    else if (desc.fileOrIdentifier == "aizana.gate")
        instance = std::make_unique<GateProcessor>();
    else if (desc.fileOrIdentifier == "aizana.saturator")
        instance = std::make_unique<SaturatorProcessor>();

    if (instance != nullptr)
    {
        instance->prepareToPlay (initialSampleRate, initialBufferSize);
        callback (std::move (instance), {});
    }
    else
    {
        callback (nullptr, "Unknown built-in plugin: " + desc.fileOrIdentifier);
    }
}
