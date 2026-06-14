#include "BuiltInPluginFormat.h"
#include "fx/EQProcessor.h"
#include "fx/CompressorProcessor.h"
#include "fx/ReverbProcessor.h"
#include "fx/LimiterProcessor.h"
#include "fx/DeEsserProcessor.h"

// ---------------------------------------------------------------------------
// Static description helpers
// ---------------------------------------------------------------------------

static juce::PluginDescription makeDesc (const juce::String& name,
                                          const juce::String& category,
                                          const juce::String& identifier)
{
    juce::PluginDescription desc;
    desc.name             = name;
    desc.pluginFormatName = "Built-in";
    desc.manufacturerName = "Cloudes";
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
    results.add (new juce::PluginDescription (makeDesc ("Cloudes EQ",         "EQ",       "cloudes.eq")));
    results.add (new juce::PluginDescription (makeDesc ("Cloudes Compressor", "Dynamics", "cloudes.compressor")));
    results.add (new juce::PluginDescription (makeDesc ("Cloudes Reverb",     "Reverb",   "cloudes.reverb")));
    results.add (new juce::PluginDescription (makeDesc ("Cloudes Limiter",    "Dynamics", "cloudes.limiter")));
    results.add (new juce::PluginDescription (makeDesc ("Cloudes De-Esser",   "Dynamics", "cloudes.deesser")));
}

juce::PluginDescription BuiltInPluginFormat::getDescriptionFor (const juce::String& fxName)
{
    if (fxName == "Cloudes EQ")         return makeDesc ("Cloudes EQ",         "EQ",       "cloudes.eq");
    if (fxName == "Cloudes Compressor") return makeDesc ("Cloudes Compressor", "Dynamics", "cloudes.compressor");
    if (fxName == "Cloudes Reverb")     return makeDesc ("Cloudes Reverb",     "Reverb",   "cloudes.reverb");
    if (fxName == "Cloudes Limiter")    return makeDesc ("Cloudes Limiter",    "Dynamics", "cloudes.limiter");
    if (fxName == "Cloudes De-Esser")   return makeDesc ("Cloudes De-Esser",   "Dynamics", "cloudes.deesser");

    jassertfalse;  // Unknown built-in FX name
    return {};
}

// ---------------------------------------------------------------------------
// AudioPluginFormat interface
// ---------------------------------------------------------------------------

void BuiltInPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                                const juce::String& fileOrIdentifier)
{
    if (fileOrIdentifier == "cloudes.eq")
        results.add (new juce::PluginDescription (makeDesc ("Cloudes EQ", "EQ", "cloudes.eq")));
    else if (fileOrIdentifier == "cloudes.compressor")
        results.add (new juce::PluginDescription (makeDesc ("Cloudes Compressor", "Dynamics", "cloudes.compressor")));
    else if (fileOrIdentifier == "cloudes.reverb")
        results.add (new juce::PluginDescription (makeDesc ("Cloudes Reverb", "Reverb", "cloudes.reverb")));
    else if (fileOrIdentifier == "cloudes.limiter")
        results.add (new juce::PluginDescription (makeDesc ("Cloudes Limiter", "Dynamics", "cloudes.limiter")));
    else if (fileOrIdentifier == "cloudes.deesser")
        results.add (new juce::PluginDescription (makeDesc ("Cloudes De-Esser", "Dynamics", "cloudes.deesser")));
}

bool BuiltInPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.startsWith ("cloudes.");
}

juce::StringArray BuiltInPluginFormat::searchPathsForPlugins (const juce::FileSearchPath&, bool, bool)
{
    return { "cloudes.eq", "cloudes.compressor", "cloudes.reverb", "cloudes.limiter", "cloudes.deesser" };
}

void BuiltInPluginFormat::createPluginInstance (const juce::PluginDescription& desc,
                                                 double initialSampleRate,
                                                 int initialBufferSize,
                                                 PluginCreationCallback callback)
{
    std::unique_ptr<juce::AudioPluginInstance> instance;

    if (desc.fileOrIdentifier == "cloudes.eq")
        instance = std::make_unique<EQProcessor>();
    else if (desc.fileOrIdentifier == "cloudes.compressor")
        instance = std::make_unique<CompressorProcessor>();
    else if (desc.fileOrIdentifier == "cloudes.reverb")
        instance = std::make_unique<ReverbProcessor>();
    else if (desc.fileOrIdentifier == "cloudes.limiter")
        instance = std::make_unique<LimiterProcessor>();
    else if (desc.fileOrIdentifier == "cloudes.deesser")
        instance = std::make_unique<DeEsserProcessor>();

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
