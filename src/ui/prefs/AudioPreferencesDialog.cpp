#include "AudioPreferencesDialog.h"

//==============================================================================

AudioPreferencesDialog::AudioPreferencesDialog (AudioEngine& engine)
    : audioEngine (engine),
      deviceSelector (engine.getDeviceManager(),
                      0,    // minAudioInputChannels
                      256,  // maxAudioInputChannels  (covers Apollo 64ch, 96ch, etc.)
                      0,    // minAudioOutputChannels
                      256,  // maxAudioOutputChannels
                      true, // showMidiInputOptions
                      true, // showMidiOutputSelector
                      true, // showChannelsAsStereoPairs (professional DAW standard)
                      false)// hideAdvancedOptionsWithButton — show all options
{
    addAndMakeVisible (deviceSelector);

    latencyLabel.setJustificationType (juce::Justification::centredLeft);
    latencyLabel.setFont (juce::FontOptions (12.0f));
    latencyLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    addAndMakeVisible (latencyLabel);

    updateLatencyLabel();
    startTimer (500);

    setSize (560, 500);
}

AudioPreferencesDialog::~AudioPreferencesDialog()
{
    stopTimer();
}

//==============================================================================

void AudioPreferencesDialog::resized()
{
    auto area = getLocalBounds();
    latencyLabel.setBounds (area.removeFromBottom (28).reduced (8, 4));
    deviceSelector.setBounds (area);
}

void AudioPreferencesDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Separator above latency strip
    g.setColour (juce::Colour (0xff444444));
    g.drawHorizontalLine (getHeight() - 28, 0.0f, (float) getWidth());
}

//==============================================================================

void AudioPreferencesDialog::timerCallback()
{
    updateLatencyLabel();
}

void AudioPreferencesDialog::updateLatencyLabel()
{
    const double ms = audioEngine.getCurrentLatencyMs();
    latencyLabel.setText ("Latency: " + juce::String (ms, 1) + " ms",
                          juce::dontSendNotification);
}

//==============================================================================

void AudioPreferencesDialog::show (AudioEngine& engine)
{
    auto* dialog = new AudioPreferencesDialog (engine);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle                  = "Audio Preferences";
    opts.dialogBackgroundColour       = juce::Colour (0xff1e1e1e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;
    opts.launchAsync();
}
