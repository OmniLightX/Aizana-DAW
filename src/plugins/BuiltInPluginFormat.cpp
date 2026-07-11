#include "BuiltInPluginFormat.h"
#include "fx/EQProcessor.h"
#include "fx/CompressorProcessor.h"
#include "fx/ReverbProcessor.h"
#include "fx/LimiterProcessor.h"
#include "fx/DeEsserProcessor.h"
#include "fx/DelayProcessor.h"
#include "fx/GateProcessor.h"
#include "fx/SaturatorProcessor.h"
#include "instruments/DrumMachineProcessor.h"
#include "instruments/SynthProcessor.h"

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
        bool        isInstrument;
    };

    // Single source of truth for all built-ins — descriptions, scan results and
    // instantiation all derive from this table. isInstrument=true entries are
    // synths (MIDI in, no audio input) and are kept OUT of the FX insert menus.
    constexpr BuiltInEntry kBuiltIns[] =
    {
        { "Aizana EQ",         "EQ",         "aizana.eq",         false },
        { "Aizana Compressor", "Dynamics",   "aizana.compressor", false },
        { "Aizana Reverb",     "Reverb",     "aizana.reverb",     false },
        { "Aizana Limiter",    "Dynamics",   "aizana.limiter",    false },
        { "Aizana De-Esser",   "Dynamics",   "aizana.deesser",    false },
        { "Aizana Delay",      "Delay",      "aizana.delay",      false },
        { "Aizana Gate",       "Dynamics",   "aizana.gate",       false },
        { "Aizana Saturator",  "Distortion", "aizana.saturator",  false },
        { "Aizana Drums",      "Instrument", "aizana.drums",      true  },
        { "Aizana Synth",      "Instrument", "aizana.synth",      true  },
    };
}

static juce::PluginDescription makeDesc (const BuiltInEntry& e)
{
    juce::PluginDescription desc;
    desc.name              = e.name;
    desc.pluginFormatName  = "Built-in";
    desc.manufacturerName  = "Aizana";
    desc.category          = e.category;
    desc.fileOrIdentifier  = e.identifier;
    desc.uniqueId          = juce::String (e.identifier).hashCode();
    desc.isInstrument      = e.isInstrument;
    desc.numInputChannels  = e.isInstrument ? 0 : 2;   // instruments have no audio input
    desc.numOutputChannels = 2;
    desc.version           = "1.0";
    return desc;
}

// ---------------------------------------------------------------------------
// BuiltInPluginFormat
// ---------------------------------------------------------------------------

void BuiltInPluginFormat::getBuiltInDescriptions (juce::OwnedArray<juce::PluginDescription>& results)
{
    for (const auto& e : kBuiltIns)
        if (! e.isInstrument)
            results.add (new juce::PluginDescription (makeDesc (e)));
}

void BuiltInPluginFormat::getBuiltInInstrumentDescriptions (juce::OwnedArray<juce::PluginDescription>& results)
{
    for (const auto& e : kBuiltIns)
        if (e.isInstrument)
            results.add (new juce::PluginDescription (makeDesc (e)));
}

void BuiltInPluginFormat::getAllBuiltInDescriptions (juce::OwnedArray<juce::PluginDescription>& results)
{
    for (const auto& e : kBuiltIns)
        results.add (new juce::PluginDescription (makeDesc (e)));
}

juce::PluginDescription BuiltInPluginFormat::getDescriptionFor (const juce::String& fxName)
{
    for (const auto& e : kBuiltIns)
        if (fxName == e.name)
            return makeDesc (e);

    jassertfalse;  // Unknown built-in name
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
            results.add (new juce::PluginDescription (makeDesc (e)));
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
    else if (desc.fileOrIdentifier == "aizana.drums")
        instance = std::make_unique<DrumMachineProcessor>();
    else if (desc.fileOrIdentifier == "aizana.synth")
        instance = std::make_unique<SynthProcessor>();

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
