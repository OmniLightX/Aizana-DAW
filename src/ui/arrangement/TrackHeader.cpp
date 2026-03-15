#include "TrackHeader.h"

//==============================================================================

TrackHeader::TrackHeader (te::AudioTrack& track_, MixerEngine& mixerEngine_)
    : track (track_)
    , mixerEngine (mixerEngine_)
{
    // Track name label
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setEditable (false, true, false);  // editable on double-click
    trackNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    trackNameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    trackNameLabel.onTextChange = [this]
    {
        track.setName (trackNameLabel.getText());
    };

    // Style mute/solo/arm buttons
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    armButton.setColour  (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    armButton.setColour  (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc3333));

    muteButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (true);
    armButton.setClickingTogglesState  (true);

    // Wire button callbacks
    muteButton.onClick = [this] { muteClicked(); };
    soloButton.onClick = [this] { soloClicked(); };
    armButton.onClick  = [this] { armClicked(); };

    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (armButton);
}

void TrackHeader::resized()
{
    auto area = getLocalBounds().reduced (2);

    // Name label takes full width at top
    trackNameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // Buttons in a row at bottom
    const int btnW = 28;
    muteButton.setBounds (area.removeFromLeft (btnW));
    area.removeFromLeft (2);
    soloButton.setBounds (area.removeFromLeft (btnW));
    area.removeFromLeft (2);
    armButton.setBounds  (area.removeFromLeft (btnW));
}

void TrackHeader::paint (juce::Graphics& g)
{
    // Track colour strip on the left edge (4px wide)
    const auto trackColour = track.getColour();
    g.setColour (trackColour);
    g.fillRect (0, 0, 4, getHeight());

    // Background
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRect (4, 0, getWidth() - 4, getHeight());

    // Right border separator
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ((float)getWidth() - 1.0f, 0.0f, (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);

    // Bottom border
    g.drawLine (0.0f, (float)getHeight() - 1.0f, (float)getWidth(), (float)getHeight() - 1.0f, 1.0f);
}

void TrackHeader::muteClicked()
{
    mixerEngine.setMute (track, muteButton.getToggleState());
}

void TrackHeader::soloClicked()
{
    mixerEngine.setSolo (track, soloButton.getToggleState());
}

void TrackHeader::armClicked()
{
    // Record-arm: set track as ready for recording.
    // Tracktion Engine uses track.setInputMonitoringEnabled() or
    // edit->recordingEnabled — store armed state in the track's ValueTree.
    if (armButton.getToggleState())
        track.setLivePlayEnabled (true);
    else
        track.setLivePlayEnabled (false);
}
