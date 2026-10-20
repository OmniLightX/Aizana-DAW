#pragma once
#include <catch2/catch_test_macros.hpp>
#include "../src/plugins/PluginScanner.h"

// Only probe deliberate fixtures. A full installed-library scan can execute
// arbitrary third-party code and AU discovery requires the message thread.
inline void scanTestPlugins(PluginScanner& scanner, bool& complete)
{
    const auto vst = juce::SystemStats::getEnvironmentVariable("AIZANA_TEST_VST3", {});
    const auto au = juce::SystemStats::getEnvironmentVariable(
        "AIZANA_TEST_AU", "AudioUnit:Effects/aufx,lmtr,appl");
    for (int i = 0; i < scanner.getFormatManager().getNumFormats(); ++i)
    {
        auto* format = scanner.getFormatManager().getFormat(i);
        const auto file = format->getName() == "VST3" ? vst
                        : format->getName() == "AudioUnit" ? au : juce::String{};
        if (file.isEmpty()) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        format->findAllTypesForFile(found, file);
        INFO("Selected plugin fixture: " << file);
        REQUIRE_FALSE(found.isEmpty());
        for (auto* desc : found) scanner.getKnownPlugins().addType(*desc);
    }
    complete = true;
}
