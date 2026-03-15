#include "DockedPluginPanel.h"

//==============================================================================
// DockedEntry

DockedPluginPanel::DockedEntry::DockedEntry (te::Plugin& p,
                                              juce::Component* ed,
                                              std::function<void()> onUnpinFn)
    : plugin (p)
    , editor (ed)
    , onUnpin (std::move (onUnpinFn))
{
    pluginLabel.setText (plugin.getName(), juce::dontSendNotification);
    pluginLabel.setJustificationType (juce::Justification::centredLeft);
    pluginLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    pluginLabel.setFont (juce::FontOptions (12.0f));

    unpinButton.setButtonText ("Pop out");
    unpinButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a3a));
    unpinButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    unpinButton.onClick = [this] { if (onUnpin) onUnpin(); };

    addAndMakeVisible (pluginLabel);
    addAndMakeVisible (unpinButton);

    if (editor != nullptr)
        addAndMakeVisible (editor);
}

void DockedPluginPanel::DockedEntry::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (kHeaderH);

    unpinButton.setBounds (headerArea.removeFromRight (70).reduced (2));
    pluginLabel.setBounds (headerArea.reduced (4, 2));

    if (editor != nullptr)
        editor->setBounds (area);
}

void DockedPluginPanel::DockedEntry::paint (juce::Graphics& g)
{
    // Dark background for the header bar
    g.fillAll (juce::Colour (0xff1e1e1e));

    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRect (0, 0, getWidth(), kHeaderH);

    // Separator line below header
    g.setColour (juce::Colour (0xff000000));
    g.drawHorizontalLine (kHeaderH - 1, 0.0f, (float)getWidth());

    // Bottom separator between entries
    g.setColour (juce::Colour (0xff111111));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());
}

//==============================================================================
// DockedPluginPanel

DockedPluginPanel::DockedPluginPanel()
{
    scrollViewport.setScrollBarsShown (true, false);  // vertical scroll only
    scrollViewport.setViewedComponent (&editorContainer, false);
    addAndMakeVisible (scrollViewport);
}

DockedPluginPanel::~DockedPluginPanel()
{
    // OwnedArray clears entries; editors are owned by entries.
}

void DockedPluginPanel::addPlugin (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Don't add the same plugin twice
    for (auto* entry : entries)
        if (&entry->plugin == &plugin)
            return;

    auto editorOwned = plugin.createEditor();
    auto* editor = editorOwned.release();

    auto onUnpin = [this, &plugin] { removePlugin (plugin); };
    auto* entry = entries.add (new DockedEntry (plugin, editor, onUnpin));
    entry->setSize (getWidth(), DockedEntry::kTotalH);
    editorContainer.addAndMakeVisible (entry);

    rebuildLayout();
    repaint();
}

void DockedPluginPanel::removePlugin (te::Plugin& plugin)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    for (int i = entries.size(); --i >= 0;)
    {
        if (&entries[i]->plugin == &plugin)
        {
            entries.remove (i);
            break;
        }
    }

    rebuildLayout();
    repaint();
}

bool DockedPluginPanel::isEmpty() const
{
    return entries.isEmpty();
}

void DockedPluginPanel::rebuildLayout()
{
    const int totalH = entries.size() * DockedEntry::kTotalH;
    editorContainer.setSize (getWidth(), juce::jmax (totalH, scrollViewport.getHeight()));

    for (int i = 0; i < entries.size(); ++i)
        entries[i]->setBounds (0, i * DockedEntry::kTotalH, getWidth(), DockedEntry::kTotalH);

    scrollViewport.setViewPosition (0, scrollViewport.getViewPositionY());
}

void DockedPluginPanel::resized()
{
    scrollViewport.setBounds (getLocalBounds());
    rebuildLayout();
}

void DockedPluginPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Left border
    g.setColour (juce::Colour (0xff333333));
    g.drawVerticalLine (0, 0.0f, (float)getHeight());
}
