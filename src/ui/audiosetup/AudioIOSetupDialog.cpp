#include "AudioIOSetupDialog.h"

//==============================================================================

AudioIOSetupDialog::AudioIOSetupDialog (te::Engine& engine_)
    : engine (engine_)
{
    titleLabel.setText ("Hardware Inputs", juce::dontSendNotification);
    titleLabel.setFont  (juce::FontOptions (14.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    closeButton.onClick = [this]
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    };
    addAndMakeVisible (closeButton);

    populate();
    setSize (480, juce::jmax (200, 60 + (int) rows.size() * 28 + 50));
}

//==============================================================================

void AudioIOSetupDialog::populate()
{
    rows.clear();

    auto& dm = engine.getDeviceManager();
    const int n = dm.getNumWaveInDevices();

    if (n == 0)
    {
        // No hardware inputs — show a placeholder row
        auto* row = rows.add (new InputRow());
        row->init (0, "(No hardware inputs detected — check Audio Device Settings)", 0, false);
        addAndMakeVisible (row);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        auto* dev = dm.getWaveInDevice (i);
        if (dev == nullptr) continue;

        auto* row = rows.add (new InputRow());
        row->init (i + 1,
                   dev->getName(),
                   0,            // channel count not exposed by TE WaveInputDevice API
                   dev->isEnabled());
        addAndMakeVisible (row);
    }
}

//==============================================================================

void AudioIOSetupDialog::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds  (area.removeFromTop (28));
    area.removeFromTop (4);

    for (auto* row : rows)
    {
        row->setBounds (area.removeFromTop (24));
        area.removeFromTop (4);
    }

    area.removeFromTop (6);
    closeButton.setBounds (area.removeFromBottom (30).removeFromRight (90));
}

void AudioIOSetupDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Separator below title
    g.setColour (juce::Colour (0xff444444));
    g.drawHorizontalLine (34, 12.0f, (float)(getWidth() - 12));
}

//==============================================================================

void AudioIOSetupDialog::show (te::Engine& engine)
{
    auto* dialog = new AudioIOSetupDialog (engine);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle                 = "Audio I/O Setup";
    opts.dialogBackgroundColour      = juce::Colour (0xff1e1e1e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar           = true;
    opts.resizable                   = false;
    opts.launchAsync();
}

//==============================================================================
// InputRow
//==============================================================================

void AudioIOSetupDialog::InputRow::init (int idx,
                                         const juce::String& name,
                                         int numChannels,
                                         bool enabled)
{
    const auto dim = juce::Colour (0xff888888);

    indexLabel.setText (idx > 0 ? juce::String (idx) : juce::String(), juce::dontSendNotification);
    indexLabel.setFont  (juce::FontOptions (11.0f));
    indexLabel.setColour (juce::Label::textColourId, dim);
    indexLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (indexLabel);

    nameLabel.setText (name, juce::dontSendNotification);
    nameLabel.setFont  (juce::FontOptions (12.0f));
    nameLabel.setColour (juce::Label::textColourId,
                         enabled ? juce::Colours::white : dim);
    addAndMakeVisible (nameLabel);

    channelLabel.setText (numChannels > 0 ? (juce::String (numChannels) + " ch") : juce::String(),
                          juce::dontSendNotification);
    channelLabel.setFont  (juce::FontOptions (11.0f));
    channelLabel.setColour (juce::Label::textColourId, dim);
    channelLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (channelLabel);

    statusLabel.setText  (enabled ? "Active" : "Disabled", juce::dontSendNotification);
    statusLabel.setFont  (juce::FontOptions (11.0f));
    statusLabel.setColour (juce::Label::textColourId,
                           enabled ? juce::Colour (0xff44aa44) : dim);
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);
}

void AudioIOSetupDialog::InputRow::resized()
{
    auto area = getLocalBounds();
    indexLabel.setBounds   (area.removeFromLeft (24));
    area.removeFromLeft (4);
    nameLabel.setBounds    (area.removeFromLeft (getWidth() - 24 - 60 - 60 - 12));
    channelLabel.setBounds (area.removeFromLeft (60));
    statusLabel.setBounds  (area);
}
