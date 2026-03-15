#include "ui/pluginbrowser/PluginBrowserPopup.h"

//==============================================================================

PluginBrowserPopup::PluginBrowserPopup (const juce::KnownPluginList& knownPlugins_,
                                        PluginSelectedCallback onSelected_)
    : knownPlugins (knownPlugins_)
    , onSelected   (std::move (onSelected_))
{
    addAndMakeVisible (searchBox);
    searchBox.setTextToShowWhenEmpty ("Search plugins...", juce::Colours::grey);
    searchBox.onTextChange = [this] { updateFilteredList (searchBox.getText()); };
    searchBox.setFont (juce::Font (14.0f));

    addAndMakeVisible (pluginListBox);
    pluginListBox.setModel (this);
    pluginListBox.setRowHeight (24);
    pluginListBox.setColour (juce::ListBox::backgroundColourId,
                             juce::Colour (0xff1e1e1e));
    pluginListBox.setColour (juce::ListBox::outlineColourId,
                             juce::Colour (0xff333333));
    pluginListBox.setOutlineThickness (1);

    // Populate with all plugins immediately
    updateFilteredList ({});

    setSize (320, 400);
}

//==============================================================================

void PluginBrowserPopup::showForComponent (juce::Component& anchor,
                                           const juce::KnownPluginList& knownPlugins,
                                           PluginSelectedCallback onSelected)
{
    auto* popup = new PluginBrowserPopup (knownPlugins, std::move (onSelected));
    juce::CallOutBox::launchAsynchronously (
        std::unique_ptr<juce::Component> (popup),
        anchor.getScreenBounds(),
        nullptr);
}

//==============================================================================

void PluginBrowserPopup::updateFilteredList (const juce::String& searchText)
{
    filteredPlugins.clear();

    const auto& allPlugins = knownPlugins.getTypes();
    for (const auto& desc : allPlugins)
    {
        if (searchText.isEmpty()
            || desc.name.containsIgnoreCase (searchText)
            || desc.manufacturerName.containsIgnoreCase (searchText))
        {
            filteredPlugins.add (desc);
        }
    }

    // Sort alphabetically by name (case-insensitive) using a comparator struct.
    // juce::Array::sort requires a type with int compareElements(T, T) const.
    struct AlphaComparator
    {
        int compareElements (const juce::PluginDescription& a,
                             const juce::PluginDescription& b) const noexcept
        {
            return a.name.compareIgnoreCase (b.name);
        }
    } cmp;
    filteredPlugins.sort (cmp);

    pluginListBox.updateContent();
}

//==============================================================================
// ListBoxModel

int PluginBrowserPopup::getNumRows()
{
    return filteredPlugins.size();
}

void PluginBrowserPopup::paintListBoxItem (int row, juce::Graphics& g,
                                           int width, int height, bool selected)
{
    if (selected)
        g.fillAll (juce::Colours::dodgerblue.withAlpha (0.6f));
    else if (row % 2 == 0)
        g.fillAll (juce::Colour (0xff252525));

    if (row < 0 || row >= filteredPlugins.size())
        return;

    const auto& desc = filteredPlugins.getReference (row);

    g.setColour (selected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont (juce::Font (13.0f));

    // Show "Name  [FORMAT]" — format tag helps distinguish VST3/AU/CLAP versions
    const juce::String label = desc.name + "  [" + desc.pluginFormatName + "]";
    g.drawText (label, 8, 0, width - 8, height, juce::Justification::centredLeft);
}

void PluginBrowserPopup::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < filteredPlugins.size())
    {
        if (onSelected)
            onSelected (filteredPlugins.getReference (row));

        // Dismiss the enclosing CallOutBox
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }
}

void PluginBrowserPopup::returnKeyPressed (int lastRow)
{
    // Trigger the same action as double-clicking; pass a dummy event
    if (lastRow >= 0 && lastRow < filteredPlugins.size())
    {
        if (onSelected)
            onSelected (filteredPlugins.getReference (lastRow));

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }
}

//==============================================================================

void PluginBrowserPopup::resized()
{
    auto area = getLocalBounds().reduced (4);
    searchBox.setBounds (area.removeFromTop (28));
    area.removeFromTop (4);
    pluginListBox.setBounds (area);
}

void PluginBrowserPopup::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    g.setColour (juce::Colour (0xff444444));
    g.drawRect (getLocalBounds(), 1);
}
