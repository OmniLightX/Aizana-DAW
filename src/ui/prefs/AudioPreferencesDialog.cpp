#include "AudioPreferencesDialog.h"

//==============================================================================

AudioPreferencesDialog::AudioPreferencesDialog (AudioEngine& engine, EditManager* em)
    : audioEngine (engine),
      editManager (em),
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

    // ---- Autosave-Intervall ----
    autosaveLabel.setFont (juce::FontOptions (12.0f));
    autosaveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    autosaveLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (autosaveLabel);

    {
        int id = 1;
        for (int m : EditManager::kAutosaveMinutesChoices)
            autosaveCombo.addItem (m == 0 ? juce::String::fromUTF8 ("aus")
                                          : juce::String (m) + " Minuten", id++);

        int sel = 1, i = 1;
        const int current = editManager != nullptr ? editManager->getAutosaveIntervalMinutes() : 5;
        for (int m : EditManager::kAutosaveMinutesChoices)
        {
            if (m == current) sel = i;
            ++i;
        }

        autosaveCombo.setSelectedId (sel, juce::dontSendNotification);
        autosaveCombo.setTitle ("Autosave Intervall");
        autosaveCombo.setTooltip (juce::String::fromUTF8 (
            "Wie oft Aizana die laufende Session automatisch sichert. "
            "Es bleiben immer nur die letzten drei Sicherungen liegen."));
        autosaveCombo.onChange = [this] { autosaveIntervalChanged(); };
        autosaveCombo.setEnabled (editManager != nullptr);
        addAndMakeVisible (autosaveCombo);
    }

    updateLatencyLabel();
    startTimer (500);

    setSize (560, 540);
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

    auto autoRow = area.removeFromBottom (30).reduced (8, 2);
    autosaveLabel.setBounds (autoRow.removeFromLeft (110));
    autoRow.removeFromLeft (6);
    autosaveCombo.setBounds (autoRow.removeFromLeft (150));

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

void AudioPreferencesDialog::show (AudioEngine& engine, EditManager* em)
{
    auto* dialog = new AudioPreferencesDialog (engine, em);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle                  = "Audio Preferences";
    opts.dialogBackgroundColour       = juce::Colour (0xff1e1e1e);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = true;
    opts.launchAsync();
}


void AudioPreferencesDialog::autosaveIntervalChanged()
{
    if (editManager == nullptr)
        return;

    const int index = autosaveCombo.getSelectedId() - 1;
    const auto& choices = EditManager::kAutosaveMinutesChoices;

    if (index >= 0 && index < (int) (sizeof (choices) / sizeof (choices[0])))
        editManager->setAutosaveIntervalMinutes (choices[index]);
}
