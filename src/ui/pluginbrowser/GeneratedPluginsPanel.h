#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class PluginScanner;

/**
    GeneratedPluginsPanel — Phase 5 / G5b: manage the Aizana-generated plugin library.

    Lists the .clap files in FaustCompiler::generatedPluginsDir() with their current display
    names and lets the user rename or delete them. Rename is a display-name override (stored
    in a sidecar, applied on every startup) — the baked-in Faust name can't change without
    recompiling. Delete moves the .clap to the Trash (recoverable). After any change it calls
    onLibraryChanged so the app re-persists the plugin cache and refreshes the browser.
*/
class GeneratedPluginsPanel : public juce::Component,
                              private juce::ListBoxModel
{
public:
    GeneratedPluginsPanel (PluginScanner& scanner, std::function<void()> onLibraryChanged);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void refresh();
    juce::File fileForRow (int row) const;
    void renameSelected();
    void deleteSelected();
    void notifyChanged();

    PluginScanner&        scanner;
    std::function<void()> onLibraryChanged;

    juce::Array<juce::File> files;

    juce::Label      header;
    juce::Label      emptyLabel;
    juce::ListBox    list;
    juce::TextButton renameButton  { juce::String::fromUTF8 ("Umbenennen\xe2\x80\xa6") };
    juce::TextButton deleteButton  { juce::String::fromUTF8 ("L\xc3\xb6schen") };
    juce::TextButton refreshButton { juce::String::fromUTF8 ("Aktualisieren") };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneratedPluginsPanel)
};
