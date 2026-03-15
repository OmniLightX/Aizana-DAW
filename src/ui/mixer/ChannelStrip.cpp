#include "ChannelStrip.h"

//==============================================================================

ChannelStrip::ChannelStrip (te::AudioTrack& track_, MixerEngine& mixerEngine_)
    : track (track_)
    , mixerEngine (mixerEngine_)
{
    // ---- Volume fader ----
    volumeFader.setSliderStyle    (juce::Slider::LinearVertical);
    volumeFader.setTextBoxStyle   (juce::Slider::TextBoxBelow, false, 50, 16);
    volumeFader.setRange          (-60.0, 6.0, 0.1);
    volumeFader.setDoubleClickReturnValue (true, 0.0);  // double-click resets to 0 dB
    volumeFader.setTextValueSuffix(" dB");
    volumeFader.setValue          (mixerEngine.getVolume (track), juce::dontSendNotification);
    volumeFader.onValueChange = [this] { volumeChanged(); };

    // ---- Pan knob ----
    panKnob.setSliderStyle     (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setTextBoxStyle    (juce::Slider::TextBoxBelow, false, 50, 16);
    panKnob.setRange           (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);  // double-click resets centre
    panKnob.setValue           (0.0, juce::dontSendNotification);  // default centre
    panKnob.onValueChange = [this] { panChanged(); };

    // ---- Solo / Mute buttons ----
    soloButton.setClickingTogglesState (true);
    muteButton.setClickingTogglesState (true);

    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));

    soloButton.onClick = [this]
    {
        mixerEngine.setSolo (track, soloButton.getToggleState());
    };
    muteButton.onClick = [this]
    {
        mixerEngine.setMute (track, muteButton.getToggleState());
    };

    // ---- Track name label ----
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setJustificationType (juce::Justification::centred);
    trackNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    trackNameLabel.setFont (juce::FontOptions (10.0f));

    addAndMakeVisible (volumeFader);
    addAndMakeVisible (panKnob);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (trackNameLabel);

    // Listen to track's ValueTree so we can sync fader when state changes externally
    // (e.g., undo/redo, AI automation writes)
    track.state.addListener (this);
}

ChannelStrip::~ChannelStrip()
{
    track.state.removeListener (this);
}

void ChannelStrip::resized()
{
    auto area = getLocalBounds().reduced (4);

    // Track name at bottom
    trackNameLabel.setBounds (area.removeFromBottom (18));
    area.removeFromBottom (2);

    // Solo + Mute buttons above name
    auto buttonArea = area.removeFromBottom (22);
    soloButton.setBounds (buttonArea.removeFromLeft (buttonArea.getWidth() / 2).reduced (1));
    muteButton.setBounds (buttonArea.reduced (1));

    // Pan knob
    const int panH = juce::jmin (50, area.getHeight() / 4);
    panKnob.setBounds (area.removeFromTop (panH));

    // Volume fader takes remaining space
    volumeFader.setBounds (area);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    // Background
    g.fillAll (juce::Colour (0xff252525));

    // Track colour strip at top
    g.setColour (track.getColour());
    g.fillRect (0, 0, getWidth(), 4);

    // Right border separator between strips
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ((float)getWidth() - 1.0f, 0.0f, (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);
}

void ChannelStrip::volumeChanged()
{
    // Each fader drag gesture should create one undo transaction.
    // beginNewTransaction is called once; subsequent value changes during the
    // same drag gesture merge into the open transaction automatically.
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Volume fader");
    mixerEngine.setVolume (track, (float)volumeFader.getValue());
}

void ChannelStrip::panChanged()
{
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Pan knob");
    mixerEngine.setPan (track, (float)panKnob.getValue());
}

void ChannelStrip::valueTreePropertyChanged (juce::ValueTree& /*tree*/,
                                              const juce::Identifier& property)
{
    // When volume or pan changes externally (undo/redo, AI), update fader/knob
    // without re-triggering the onChange callback (dontSendNotification).
    using te::IDs;
    if (property == te::IDs::volume)
    {
        const float dB = mixerEngine.getVolume (track);
        volumeFader.setValue (dB, juce::dontSendNotification);
    }
}
