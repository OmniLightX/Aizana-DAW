#include "AudioIOSetupDialog.h"

//==============================================================================

AudioIOSetupDialog::AudioIOSetupDialog (te::Engine& engine_, InputBusManager& busManager_)
    : engine (engine_), busManager (busManager_)
{
    titleLabel.setText ("Named Input Buses", juce::dontSendNotification);
    titleLabel.setFont  (juce::FontOptions (14.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    hintLabel.setText ("Map a stable name to a hardware input. Tracks select buses, not raw hardware.",
                       juce::dontSendNotification);
    hintLabel.setFont  (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    addAndMakeVisible (hintLabel);

    busViewport.setViewedComponent (&busListContainer, false);
    busViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (busViewport);

    addBusButton.onClick = [this]
    {
        if (hwInputs.isEmpty())
            return;  // no hardware inputs to map a bus to

        // Default the new bus to the first hardware input not already used by a bus,
        // falling back to the first input if all are taken.
        int channel = hwInputs.getFirst().channelIndex;
        juce::String name = hwInputs.getFirst().name;
        const auto existing = busManager.getBuses();
        for (const auto& hw : hwInputs)
        {
            bool used = false;
            for (const auto& b : existing)
                if (b.hwChannel == hw.channelIndex) { used = true; break; }
            if (! used) { channel = hw.channelIndex; name = hw.name; break; }
        }

        busManager.createBus (name, channel, false);
        busManager.applyToEngine();
        rebuildBuses();
    };
    addAndMakeVisible (addBusButton);

    closeButton.onClick = [this]
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    };
    addAndMakeVisible (closeButton);

    buildHwInputList();
    rebuildBuses();

    setSize (520, 440);
}

//==============================================================================

void AudioIOSetupDialog::buildHwInputList()
{
    hwInputs.clear();

    auto& dm = engine.getDeviceManager();
    for (auto* dev : dm.getWaveInputDevices())
    {
        if (dev == nullptr) continue;
        const auto& channels = dev->getChannels();
        if (channels.empty()) continue;
        hwInputs.add ({ dev->getName(), channels.front().indexInDevice });
    }
}

int AudioIOSetupDialog::comboIndexForChannel (int channelIndex) const
{
    for (int i = 0; i < hwInputs.size(); ++i)
        if (hwInputs[i].channelIndex == channelIndex)
            return i;
    return -1;
}

//==============================================================================

void AudioIOSetupDialog::rebuildBuses()
{
    rows.clear();

    const auto buses = busManager.getBuses();
    for (int i = 0; i < buses.size(); ++i)
    {
        auto* row = rows.add (new BusRow());

        row->nameEditor.setText (buses[i].name, juce::dontSendNotification);

        // Channel combo lists every hardware input; select the one this bus maps to.
        int id = 1;
        for (const auto& hw : hwInputs)
            row->channelCombo.addItem (hw.name, id++);
        const int sel = comboIndexForChannel (buses[i].hwChannel);
        if (sel >= 0)
            row->channelCombo.setSelectedItemIndex (sel, juce::dontSendNotification);

        // Capture the row index. Every structural change (add/delete) calls
        // rebuildBuses(), which recreates all rows with fresh, correct indices,
        // so a captured index is valid for the lifetime of this row.
        const int index = i;

        row->onRename = [this, index] (const juce::String& newName)
        {
            busManager.renameBus (index, newName);
            busManager.applyToEngine();
        };

        row->onRemap = [this, index] (int comboItemIndex)
        {
            if (comboItemIndex < 0 || comboItemIndex >= hwInputs.size())
                return;
            busManager.remapBus (index, hwInputs[comboItemIndex].channelIndex, false);
            busManager.applyToEngine();
        };

        row->onDelete = [this, index]
        {
            busManager.deleteBus (index);
            busManager.applyToEngine();
            rebuildBuses();
        };

        busListContainer.addAndMakeVisible (row);
    }

    // Size the scrolled container to hold all rows, then lay them out.
    const int w = juce::jmax (1, busViewport.getMaximumVisibleWidth());
    busListContainer.setSize (w, juce::jmax (kRowHeight, rows.size() * kRowHeight));

    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, w, kRowHeight - 4);
        y += kRowHeight;
    }
}

//==============================================================================

void AudioIOSetupDialog::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds (area.removeFromTop (24));
    hintLabel.setBounds  (area.removeFromTop (18));
    area.removeFromTop (6);

    // Bottom button row: Add Bus (left) + Close (right)
    auto buttonRow = area.removeFromBottom (30);
    addBusButton.setBounds (buttonRow.removeFromLeft (100));
    closeButton.setBounds  (buttonRow.removeFromRight (90));
    area.removeFromBottom (8);

    busViewport.setBounds (area);

    // Keep the scrolled container width in sync with the viewport, then re-layout rows.
    const int w = juce::jmax (1, busViewport.getMaximumVisibleWidth());
    busListContainer.setSize (w, juce::jmax (kRowHeight, rows.size() * kRowHeight));
    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, w, kRowHeight - 4);
        y += kRowHeight;
    }
}

void AudioIOSetupDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Separator below the title + hint
    g.setColour (juce::Colour (0xff444444));
    g.drawHorizontalLine (54, 12.0f, (float) (getWidth() - 12));
}

//==============================================================================

void AudioIOSetupDialog::show (te::Engine& engine, InputBusManager& busManager)
{
    auto* dialog = new AudioIOSetupDialog (engine, busManager);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle                  = "Audio I/O Setup";
    opts.dialogBackgroundColour       = juce::Colour (0xff1e1e1e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;
    opts.launchAsync();
}

//==============================================================================
// BusRow
//==============================================================================

AudioIOSetupDialog::BusRow::BusRow()
{
    nameEditor.setEditable (true, true, false);  // edit on single or double click
    nameEditor.setColour (juce::Label::textColourId,        juce::Colours::white);
    nameEditor.setColour (juce::Label::backgroundColourId,  juce::Colour (0xff2a2a2a));
    nameEditor.setColour (juce::Label::outlineColourId,     juce::Colour (0xff444444));
    nameEditor.setFont   (juce::FontOptions (12.0f));
    nameEditor.onTextChange = [this]
    {
        if (onRename)
            onRename (nameEditor.getText());
    };
    addAndMakeVisible (nameEditor);

    channelCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
    channelCombo.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
    channelCombo.onChange = [this]
    {
        if (onRemap)
            onRemap (channelCombo.getSelectedItemIndex());
    };
    addAndMakeVisible (channelCombo);

    deleteButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")));  // ✕
    deleteButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3a));
    deleteButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcc5555));
    deleteButton.setTooltip ("Delete bus");
    deleteButton.onClick = [this]
    {
        if (onDelete)
            onDelete();
    };
    addAndMakeVisible (deleteButton);
}

void AudioIOSetupDialog::BusRow::resized()
{
    auto area = getLocalBounds();
    deleteButton.setBounds (area.removeFromRight (26));
    area.removeFromRight (6);
    channelCombo.setBounds (area.removeFromRight (200));
    area.removeFromRight (6);
    nameEditor.setBounds   (area);
}
