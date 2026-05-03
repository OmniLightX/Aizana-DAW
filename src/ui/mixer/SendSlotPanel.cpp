#include "SendSlotPanel.h"

//==============================================================================
// SendSlotPanel::SendRow

SendSlotPanel::SendRow::SendRow (te::AudioTrack& sourceTrack_,
                                  te::AudioTrack& fxChannel_,
                                  MixerEngine&    mixerEngine_)
    : fxChannel (fxChannel_)
    , sourceTrack (sourceTrack_)
    , mixerEngine (mixerEngine_)
{
    // Dot toggle — green = send active, grey = inactive
    dotButton.setClickingTogglesState (false);
    dotButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a3a));
    dotButton.onClick = [this]
    {
        const bool nowEnabled = mixerEngine.isFXSendEnabled (sourceTrack, fxChannel);
        if (! nowEnabled)
        {
            // Create the send at -6 dB if it doesn't exist yet
            if (mixerEngine.getFXSendLevel (sourceTrack, fxChannel) <= -99.0f)
                mixerEngine.createFXSend (sourceTrack, fxChannel, -6.0f);
            else
                mixerEngine.setFXSendEnabled (sourceTrack, fxChannel, true);
        }
        else
        {
            mixerEngine.setFXSendEnabled (sourceTrack, fxChannel, false);
        }
        syncDotColour();
        levelSlider.repaint();
    };

    // FX channel name label
    nameLabel.setText (fxChannel.getName(), juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (8.5f));
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    nameLabel.setJustificationType (juce::Justification::centredLeft);

    // Level slider — horizontal mini slider, -60 to 0 dB, no text box
    levelSlider.setSliderStyle  (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    levelSlider.setRange        (-60.0, 0.0, 0.1);
    levelSlider.setDoubleClickReturnValue (true, -6.0);
    levelSlider.setTooltip ("Send level (dB) — double-click to reset to -6 dB");

    const float currentLevel = mixerEngine.getFXSendLevel (sourceTrack, fxChannel);
    levelSlider.setValue (currentLevel > -99.0f ? (double)currentLevel : -6.0,
                          juce::dontSendNotification);

    levelSlider.onValueChange = [this]
    {
        const float level = (float)levelSlider.getValue();
        if (mixerEngine.getFXSendLevel (sourceTrack, fxChannel) <= -99.0f)
            mixerEngine.createFXSend (sourceTrack, fxChannel, level);
        else
            mixerEngine.setFXSendLevel (sourceTrack, fxChannel, level);
        syncDotColour();
    };

    addAndMakeVisible (dotButton);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (levelSlider);

    syncDotColour();
}

void SendSlotPanel::SendRow::syncDotColour()
{
    const bool exists  = mixerEngine.getFXSendLevel (sourceTrack, fxChannel) > -99.0f;
    const bool enabled = exists && mixerEngine.isFXSendEnabled (sourceTrack, fxChannel);
    dotButton.setColour (juce::TextButton::buttonColourId,
                         enabled ? juce::Colour (0xff33aa33) : juce::Colour (0xff3a3a3a));
    dotButton.repaint();
}

void SendSlotPanel::SendRow::paint (juce::Graphics& g)
{
    // Subtle separator at the bottom of each row
    g.setColour (juce::Colour (0xff111111));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());
}

void SendSlotPanel::SendRow::resized()
{
    auto area = getLocalBounds().reduced (0, 1);  // 1px top/bottom margin

    const int kDotW  = 12;
    const int kNameW = 44;

    dotButton.setBounds   (area.removeFromLeft (kDotW));
    area.removeFromLeft (2);
    nameLabel.setBounds   (area.removeFromLeft (kNameW));
    area.removeFromLeft (2);
    levelSlider.setBounds (area);  // remaining width
}

//==============================================================================
// SendSlotPanel

SendSlotPanel::SendSlotPanel (te::AudioTrack& track_, MixerEngine& mixerEngine_)
    : track (track_)
    , mixerEngine (mixerEngine_)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void SendSlotPanel::rebuild (const juce::Array<te::AudioTrack*>& fxChannels)
{
    rows.clear();
    content.removeAllChildren();

    for (auto* fxCh : fxChannels)
    {
        auto* row = rows.add (new SendRow (track, *fxCh, mixerEngine));
        content.addAndMakeVisible (row);
    }

    resized();
    repaint();
}

void SendSlotPanel::resized()
{
    viewport.setBounds (getLocalBounds());

    const int contentW = juce::jmax (1, getWidth());
    const int contentH = juce::jmax (kRowHeight, rows.size() * kRowHeight);
    content.setSize (contentW, contentH);

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * kRowHeight, contentW, kRowHeight);
}

void SendSlotPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
}
