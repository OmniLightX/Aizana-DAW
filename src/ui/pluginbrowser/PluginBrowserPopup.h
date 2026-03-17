#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "plugins/PluginScanner.h"

// Callback type: called when user selects a plugin
using PluginSelectedCallback = std::function<void(const juce::PluginDescription&)>;

/**
 * PluginBrowserPopup — Searchable list of scanned plugins shown in a CallOutBox.
 *
 * Populated from PluginScanner::getKnownPlugins() — shows VST3, AU, and CLAP
 * plugins in a scrollable list with real-time name/manufacturer search filter.
 *
 * Double-clicking a row calls the onSelected callback and dismisses the popup.
 * Press Return on a selected row does the same.
 */
class PluginBrowserPopup : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::Timer
{
public:
    PluginBrowserPopup (const juce::KnownPluginList& knownPlugins,
                        PluginSelectedCallback onSelected);
    ~PluginBrowserPopup() override;

    /**
     * Show as a CallOutBox anchored to the given component.
     * The popup is heap-allocated and owns itself after launch.
     */
    static void showForComponent (juce::Component& anchor,
                                  const juce::KnownPluginList& knownPlugins,
                                  PluginSelectedCallback onSelected);

    void resized() override;
    void paint  (juce::Graphics& g) override;

private:
    const juce::KnownPluginList& knownPlugins;
    PluginSelectedCallback       onSelected;

    juce::TextEditor searchBox;
    juce::ListBox    pluginListBox;
    juce::Array<juce::PluginDescription> filteredPlugins;

    // ListBoxModel overrides
    int  getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed         (int lastRowSelected)             override;

    void updateFilteredList (const juce::String& searchText);

    // Timer — polls KnownPluginList once per second to detect new scan results
    void timerCallback() override;

    int lastKnownPluginCount = 0;  // tracks count to detect changes from a background scan

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserPopup)
};
