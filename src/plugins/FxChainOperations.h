#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace aizana
{
namespace te = tracktion::engine;
/** Message-thread-only FX snapshots and one-transaction edits, including Master. */
struct FxChainOperations
{
    static bool isEffect(te::Plugin &);
    static juce::Array<te::Plugin *> effects(te::PluginList &);
    static int insertionIndex(te::PluginList &, int slot = -1);
    static juce::ValueTree snapshot(te::PluginList &, te::Plugin *single = nullptr);
    static juce::Result apply(te::PluginList &, const juce::ValueTree &, int slot = -1, bool replace = false);
    static juce::Result move(te::PluginList &source, te::PluginList &target, te::Plugin *single, int slot);
    static void reorder(te::PluginList &, te::Plugin &, int insertionSlot);
    static void setEnabled(te::PluginList &, bool);
    static void removeAll(te::PluginList &);
    static juce::Result save(const juce::File &, const juce::ValueTree &);
    static juce::Result load(const juce::File &, juce::ValueTree &);
    static juce::File presetDirectory();
};
} // namespace aizana
