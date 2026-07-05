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
    results.add (new juce::PluginDescription (makeDesc ("Aizana EQ",         "EQ",       "aizana.eq")));
    results.add (new juce::PluginDescription (makeDesc ("Aizana Compressor", "Dynamics", "aizana.compressor")));
    results.add (new juce::PluginDescription (makeDesc ("Aizana Reverb",     "Reverb",   "aizana.reverb")));
    results.add (new juce::PluginDescription (makeDesc ("Aizana Limiter",    "Dynamics", "aizana.limiter")));
    results.add (new juce::PluginDescription (makeDesc ("Aizana De-Esser",   "Dynamics", "aizana.deesser")));
}

juce::PluginDescription BuiltInPluginFormat::getDescriptionFor (const juce::String& fxName)
{
    if (fxName == "Aizana EQ")         return makeDesc ("Aizana EQ",         "EQ",       "aizana.eq");
    if (fxName == "Aizana Compressor") return makeDesc ("Aizana Compressor", "Dynamics", "aizana.compressor");
    if (fxName == "Aizana Reverb")     return makeDesc ("Aizana Reverb",     "Reverb",   "aizana.reverb");
    if (fxName == "Aizana Limiter")    return makeDesc ("Aizana Limiter",    "Dynamics", "aizana.limiter");
    if (fxName == "Aizana De-Esser")   return makeDesc ("Aizana De-Esser",   "Dynamics", "aizana.deesser");

    jassertfalse;  // Unknown built-in FX name
    return {};
}

// ---------------------------------------------------------------------------
// AudioPluginFormat interface
// ---------------------------------------------------------------------------

void BuiltInPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                                const juce::String& fileOrIdentifier)
{
    if (fileOrIdentifier == "aizana.eq")
        results.add (new juce::PluginDescription (makeDesc ("Aizana EQ", "EQ", "aizana.eq")));
    else if (fileOrIdentifier == "aizana.compressor")
        results.add (new juce::PluginDescription (makeDesc ("Aizana Compressor", "Dynamics", "aizana.compressor")));
    else if (fileOrIdentifier == "aizana.reverb")
        results.add (new juce::PluginDescription (makeDesc ("Aizana Reverb", "Reverb", "aizana.reverb")));
    else if (fileOrIdentifier == "aizana.limiter")
        results.add (new juce::PluginDescription (makeDesc ("Aizana Limiter", "Dynamics", "aizana.limiter")));
    else if (fileOrIdentifier == "aizana.deesser")
        results.add (new juce::PluginDescription (makeDesc ("Aizana De-Esser", "Dynamics", "aizana.deesser")));
}

bool BuiltInPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.startsWith ("aizana.");
}

juce::StringArray BuiltInPluginFormat::searchPathsForPlugins (const juce::FileSearchPath&, bool, bool)
{
    return { "aizana.eq", "aizana.compressor", "aizana.reverb", "aizana.limiter", "aizana.deesser" };
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
