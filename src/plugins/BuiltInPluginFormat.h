#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/**
 * BuiltInPluginFormat — Custom AudioPluginFormat for Aizana' built-in FX.
 *
 * Allows EQProcessor, CompressorProcessor, ReverbProcessor, LimiterProcessor
 * to be inserted into TE's plugin chain via the normal ExternalPlugin path.
 * TE's ExternalPlugin uses AudioPluginFormatManager::createPluginInstance(),
 * which delegates to this format when pluginFormatName matches "Built-in".
 *
 * Processors inherit AudioPluginInstance (not plain AudioProcessor) so they
 * can be returned from createPluginInstance() as std::unique_ptr<AudioPluginInstance>.
 */
class BuiltInPluginFormat : public juce::AudioPluginFormat
{
public:
    BuiltInPluginFormat() = default;

    juce::String getName() const override { return "Built-in"; }

    /** Populates descriptions for the built-in FX only (no instruments) — this is
        what the mixer/channel insert menus offer. */
    static void getBuiltInDescriptions (juce::OwnedArray<juce::PluginDescription>& results);

    /** Populates descriptions for the built-in INSTRUMENTS only (Aizana Drums / Synth). */
    static void getBuiltInInstrumentDescriptions (juce::OwnedArray<juce::PluginDescription>& results);

    /** FX + instruments together — used to register everything with the engine's
        knownPluginList so ExternalPlugin::findMatchingPlugin() can locate them. */
    static void getAllBuiltInDescriptions (juce::OwnedArray<juce::PluginDescription>& results);

    /** Returns a PluginDescription for a specific built-in (FX or instrument) by display name. */
    static juce::PluginDescription getDescriptionFor (const juce::String& fxName);

    // ---------------------------------------------------------------------------
    // AudioPluginFormat interface
    // ---------------------------------------------------------------------------
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&,
                              const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const noexcept override { return false; }
    bool isTrivialToScan() const override { return true; }

protected:
    void createPluginInstance (const juce::PluginDescription& desc,
                               double initialSampleRate,
                               int initialBufferSize,
                               PluginCreationCallback callback) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInPluginFormat)
};
