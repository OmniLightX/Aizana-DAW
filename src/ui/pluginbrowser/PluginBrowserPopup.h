#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "plugins/PluginScanner.h"

// Callback type: called when user selects a plugin
using PluginSelectedCallback = std::function<void(const juce::PluginDescription&)>;

/**
 * PluginBrowserPopup — Searchable list of scanned plugins shown in a CallOutBox.
 *
 * Populated from PluginScanner::getKnownPlugins() — shows VST3, AU, and CLAP
 * plugins grouped under collapsible manufacturer headers (Logic/Studio One style)
 * so large libraries don't force scrolling from A to Z. Groups start collapsed;
 * clicking a header expands/collapses it. A real-time name/manufacturer search
 * filter force-expands all matching groups while text is present.
 *
 * Double-clicking a plugin row calls the onSelected callback and dismisses the
 * popup; Return on a selected plugin row does the same. Click/Return on a header
 * toggles its group.
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

    // A display row is either a collapsible manufacturer header or a plugin entry.
    struct Row
    {
        enum class Type { Header, Plugin };
        Type type = Type::Plugin;
        juce::String manufacturer;            // header label / a plugin's manufacturer
        int          pluginCount = 0;         // header only
        juce::PluginDescription desc;         // plugin only
    };

    juce::Array<Row>  displayRows;            // flattened header+plugin rows for the ListBox
    juce::StringArray collapsedManufacturers; // manufacturers currently collapsed
    bool              searchActive       = false;  // force-expand all groups while filtering
    bool              defaultCollapseDone = false; // groups start collapsed on first build

    // ListBoxModel overrides
    int  getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked       (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed         (int lastRowSelected)             override;

    void rebuildDisplayRows (const juce::String& searchText);
    void toggleManufacturer (const juce::String& manufacturer);

    // Timer — polls KnownPluginList once per second to detect new scan results
    void timerCallback() override;

    int lastKnownPluginCount = 0;  // tracks count to detect changes from a background scan

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowserPopup)
};
