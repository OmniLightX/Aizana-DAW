#include "ui/pluginbrowser/PluginBrowserPopup.h"

//==============================================================================

PluginBrowserPopup::PluginBrowserPopup (const juce::KnownPluginList& knownPlugins_,
                                        PluginSelectedCallback onSelected_)
    : knownPlugins (knownPlugins_)
    , onSelected   (std::move (onSelected_))
{
    addAndMakeVisible (searchBox);
    searchBox.setTextToShowWhenEmpty ("Search plugins...", juce::Colours::grey);
    searchBox.onTextChange = [this] { rebuildDisplayRows (searchBox.getText()); };
    searchBox.setFont (juce::Font (14.0f));

    addAndMakeVisible (pluginListBox);
    pluginListBox.setModel (this);
    pluginListBox.setRowHeight (24);
    pluginListBox.setColour (juce::ListBox::backgroundColourId,
                             juce::Colour (0xff1e1e1e));
    pluginListBox.setColour (juce::ListBox::outlineColourId,
                             juce::Colour (0xff333333));
    pluginListBox.setOutlineThickness (1);

    // Populate immediately — groups start collapsed (manufacturer headers only)
    rebuildDisplayRows ({});

    // Track current count so timerCallback can detect new scan results
    lastKnownPluginCount = knownPlugins.getNumTypes();

    // Poll once per second — refreshes list if a background scan completes
    // while this popup is open (the KnownPluginList& reference is always live)
    startTimerHz (1);

    setSize (320, 400);
}

PluginBrowserPopup::~PluginBrowserPopup()
{
    stopTimer();
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

void PluginBrowserPopup::rebuildDisplayRows (const juce::String& searchText)
{
    searchActive = searchText.isNotEmpty();
    displayRows.clear();

    const auto allPlugins = knownPlugins.getTypes();  // value copy — avoids data race with background scan thread

    // Normalise a plugin's manufacturer (empty → "Unknown") so grouping is stable.
    auto makerOf = [] (const juce::PluginDescription& d) -> juce::String
    {
        const auto m = d.manufacturerName.trim();
        return m.isEmpty() ? juce::String ("Unknown") : m;
    };

    // Collect the matching plugins and the set of manufacturers they belong to.
    juce::Array<juce::PluginDescription> matches;
    juce::StringArray manufacturers;
    for (const auto& desc : allPlugins)
    {
        if (searchActive
            && ! desc.name.containsIgnoreCase (searchText)
            && ! desc.manufacturerName.containsIgnoreCase (searchText))
            continue;

        matches.add (desc);
        manufacturers.addIfNotAlreadyThere (makerOf (desc));
    }

    manufacturers.sort (true);  // case-insensitive

    // First build: start with every group collapsed so the popup opens compact.
    if (! defaultCollapseDone)
    {
        collapsedManufacturers = manufacturers;
        defaultCollapseDone = true;
    }

    // Sort plugins within a group alphabetically by name (case-insensitive).
    struct AlphaComparator
    {
        int compareElements (const juce::PluginDescription& a,
                             const juce::PluginDescription& b) const noexcept
        {
            return a.name.compareIgnoreCase (b.name);
        }
    } cmp;

    for (const auto& maker : manufacturers)
    {
        juce::Array<juce::PluginDescription> group;
        for (const auto& desc : matches)
            if (makerOf (desc) == maker)
                group.add (desc);
        group.sort (cmp);

        Row header;
        header.type         = Row::Type::Header;
        header.manufacturer = maker;
        header.pluginCount  = group.size();
        displayRows.add (header);

        // Expand while searching, or when not explicitly collapsed.
        const bool expanded = searchActive || ! collapsedManufacturers.contains (maker);
        if (expanded)
        {
            for (const auto& desc : group)
            {
                Row r;
                r.type         = Row::Type::Plugin;
                r.manufacturer = maker;
                r.desc         = desc;
                displayRows.add (r);
            }
        }
    }

    pluginListBox.updateContent();
    repaint();
}

void PluginBrowserPopup::toggleManufacturer (const juce::String& manufacturer)
{
    if (searchActive)
        return;  // groups are force-expanded while a filter is active

    if (collapsedManufacturers.contains (manufacturer))
        collapsedManufacturers.removeString (manufacturer);
    else
        collapsedManufacturers.add (manufacturer);

    rebuildDisplayRows (searchBox.getText());
}

//==============================================================================

void PluginBrowserPopup::timerCallback()
{
    const int currentCount = knownPlugins.getNumTypes();
    if (currentCount != lastKnownPluginCount)
    {
        lastKnownPluginCount = currentCount;
        rebuildDisplayRows (searchBox.getText());
    }
}

//==============================================================================
// ListBoxModel

int PluginBrowserPopup::getNumRows()
{
    return displayRows.size();
}

void PluginBrowserPopup::paintListBoxItem (int row, juce::Graphics& g,
                                           int width, int height, bool selected)
{
    if (row < 0 || row >= displayRows.size())
        return;

    const auto& item = displayRows.getReference (row);

    if (item.type == Row::Type::Header)
    {
        // Manufacturer header — darker bar with a disclosure triangle, name + count.
        g.fillAll (juce::Colour (0xff2d2d33));
        g.setColour (juce::Colour (0xff3a3a40));
        g.drawHorizontalLine (height - 1, 0.0f, (float) width);

        const bool expanded = searchActive || ! collapsedManufacturers.contains (item.manufacturer);

        // Disclosure triangle: pointing right when collapsed, down when expanded.
        juce::Path tri;
        const float cx = 11.0f, cy = height * 0.5f, s = 4.0f;
        if (expanded)
            tri.addTriangle (cx - s, cy - s * 0.55f,
                             cx + s, cy - s * 0.55f,
                             cx,     cy + s * 0.85f);
        else
            tri.addTriangle (cx - s * 0.55f, cy - s,
                             cx - s * 0.55f, cy + s,
                             cx + s * 0.85f, cy);
        g.setColour (juce::Colour (0xffaaaaaa));
        g.fillPath (tri);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (item.manufacturer, 24, 0, width - 24 - 44, height,
                    juce::Justification::centredLeft, true);

        g.setColour (juce::Colour (0xff888888));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("(" + juce::String (item.pluginCount) + ")",
                    width - 44, 0, 40, height, juce::Justification::centredRight);
        return;
    }

    // Plugin row — indented under its manufacturer header.
    if (selected)
        g.fillAll (juce::Colours::dodgerblue.withAlpha (0.6f));
    else
        g.fillAll (juce::Colour (0xff1e1e1e));

    g.setColour (selected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));

    // Show "Name  [FORMAT]" — format tag helps distinguish VST3/AU/CLAP versions
    const juce::String label = item.desc.name + "  [" + item.desc.pluginFormatName + "]";
    g.drawText (label, 24, 0, width - 28, height, juce::Justification::centredLeft);
}

void PluginBrowserPopup::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    // A single click on a manufacturer header expands/collapses its group.
    if (row >= 0 && row < displayRows.size()
        && displayRows.getReference (row).type == Row::Type::Header)
        toggleManufacturer (displayRows.getReference (row).manufacturer);
}

void PluginBrowserPopup::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= displayRows.size())
        return;

    const auto& item = displayRows.getReference (row);
    if (item.type == Row::Type::Header)
        return;  // headers toggle on single click (listBoxItemClicked); double-click = open plugin only

    if (onSelected)
        onSelected (item.desc);

    // Dismiss the enclosing CallOutBox
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

void PluginBrowserPopup::returnKeyPressed (int lastRow)
{
    if (lastRow < 0 || lastRow >= displayRows.size())
        return;

    const auto& item = displayRows.getReference (lastRow);
    if (item.type == Row::Type::Header)
    {
        toggleManufacturer (item.manufacturer);
        return;
    }

    if (onSelected)
        onSelected (item.desc);

    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
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

    // Empty state: draw a centred hint in the list area when no plugins are found.
    // Apple approach: quiet, informative, not alarming.
    if (displayRows.isEmpty())
    {
        const auto listBounds = pluginListBox.getBounds();

        g.setColour (juce::Colour (0xff555555));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("No plugins found",
                    listBounds.toFloat(),
                    juce::Justification::centred, false);

        g.setFont (juce::FontOptions (10.0f));
        g.setColour (juce::Colour (0xff444444));
        // "\xe2\x80\xba" = UTF-8 encoding of › (right-pointing angle quotation mark)
        g.drawText (juce::String::fromUTF8 ("Run View \xe2\x80\xba Scan Plugins..."),
                    listBounds.withTrimmedTop (listBounds.getHeight() / 2).toFloat(),
                    juce::Justification::centred, false);
    }
}
