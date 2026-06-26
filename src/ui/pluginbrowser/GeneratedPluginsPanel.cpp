#include "GeneratedPluginsPanel.h"

#include "plugins/PluginScanner.h"
#include "generator/FaustCompiler.h"

//==============================================================================
GeneratedPluginsPanel::GeneratedPluginsPanel (PluginScanner& s, std::function<void()> onChanged)
    : scanner (s), onLibraryChanged (std::move (onChanged))
{
    header.setText (juce::String::fromUTF8 ("Cloudes-Plugins \xe2\x80\x94 generierte Plugins verwalten"),
                    juce::dontSendNotification);
    header.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    header.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (header);

    emptyLabel.setText (juce::String::fromUTF8 ("Noch keine generierten Plugins. Erzeuge eines \xc3\xbc" "ber "
                                                "\xe2\x80\x9e" "KI-Studio\xe2\x80\x9c oder \xe2\x80\x9e" "Plugin generieren\xe2\x80\xa6\xe2\x80\x9c."),
                        juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addChildComponent (emptyLabel);

    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141414));
    addAndMakeVisible (list);

    renameButton.onClick = [this] { renameSelected(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    refreshButton.onClick = [this] { refresh(); };
    addAndMakeVisible (renameButton);
    addAndMakeVisible (deleteButton);
    addAndMakeVisible (refreshButton);

    refresh();
}

//==============================================================================
void GeneratedPluginsPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    header.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);

    auto buttons = r.removeFromBottom (32);
    refreshButton.setBounds (buttons.removeFromLeft (110));
    deleteButton .setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    renameButton .setBounds (buttons.removeFromRight (120));
    r.removeFromBottom (8);

    list.setBounds (r);
    emptyLabel.setBounds (r);
}

void GeneratedPluginsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

//==============================================================================
void GeneratedPluginsPanel::refresh()
{
    files.clear();

    const auto dir = cloudes::FaustCompiler::generatedPluginsDir();
    if (dir.isDirectory())
    {
        files = dir.findChildFiles (juce::File::findFiles, false, "*.clap");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return PluginScanner::generatedDisplayName (a)
                       .compareIgnoreCase (PluginScanner::generatedDisplayName (b)) < 0;
        });
    }

    list.updateContent();
    list.deselectAllRows();
    emptyLabel.setVisible (files.isEmpty());
    list.setVisible (! files.isEmpty());

    renameButton.setEnabled (false);
    deleteButton.setEnabled (false);
}

juce::File GeneratedPluginsPanel::fileForRow (int row) const
{
    return juce::isPositiveAndBelow (row, files.size()) ? files[row] : juce::File();
}

int GeneratedPluginsPanel::getNumRows()
{
    return files.size();
}

void GeneratedPluginsPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto file = fileForRow (row);
    if (file == juce::File())
        return;

    if (selected)
        g.fillAll (juce::Colour (0xff3a3a55));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (PluginScanner::generatedDisplayName (file),
                10, 4, w - 20, h / 2, juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (file.getFileName(),
                10, h / 2, w - 20, h / 2 - 4, juce::Justification::centredLeft);
}

void GeneratedPluginsPanel::selectedRowsChanged (int lastRow)
{
    const bool has = juce::isPositiveAndBelow (lastRow, files.size());
    renameButton.setEnabled (has);
    deleteButton.setEnabled (has);
}

void GeneratedPluginsPanel::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    renameSelected();
}

//==============================================================================
void GeneratedPluginsPanel::notifyChanged()
{
    if (onLibraryChanged)
        onLibraryChanged();
}

void GeneratedPluginsPanel::renameSelected()
{
    const auto file = fileForRow (list.getSelectedRow());
    if (file == juce::File())
        return;

    const auto current = PluginScanner::generatedDisplayName (file);

    auto aw = std::make_unique<juce::AlertWindow> (
        juce::String::fromUTF8 ("Plugin umbenennen"),
        juce::String::fromUTF8 ("Neuer Anzeigename f\xc3\xbcr \xe2\x80\x9e") + current + juce::String::fromUTF8 ("\xe2\x80\x9c:"),
        juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("name", current);
    aw->addButton (juce::String::fromUTF8 ("Umbenennen"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setVisible (true);

    auto* awPtr = aw.get();
    awPtr->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<GeneratedPluginsPanel> (this),
             awPtr, owner = std::move (aw), file] (int result) mutable
            {
                const auto newName = awPtr->getTextEditorContents ("name");
                owner.reset();   // close + delete the window now

                if (safe == nullptr || result == 0 || newName.trim().isEmpty())
                    return;

                if (safe->scanner.renameGeneratedPlugin (file, newName))
                {
                    safe->refresh();
                    safe->notifyChanged();
                }
            }),
        false);
}

void GeneratedPluginsPanel::deleteSelected()
{
    const auto file = fileForRow (list.getSelectedRow());
    if (file == juce::File())
        return;

    const auto name = PluginScanner::generatedDisplayName (file);

    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        juce::String::fromUTF8 ("Plugin l\xc3\xb6schen"),
        juce::String::fromUTF8 ("\xe2\x80\x9e") + name + juce::String::fromUTF8 ("\xe2\x80\x9c in den Papierkorb verschieben? "
            "Das Plugin verschwindet aus der Library; bestehende Sessions, die es nutzen, "
            "k\xc3\xb6nnen es danach nicht mehr laden."),
        juce::String::fromUTF8 ("L\xc3\xb6schen"),
        "Abbrechen",
        nullptr,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<GeneratedPluginsPanel> (this), file] (int result)
            {
                if (safe == nullptr || result == 0)
                    return;

                if (safe->scanner.deleteGeneratedPlugin (file))
                {
                    safe->refresh();
                    safe->notifyChanged();
                }
            }));
}
