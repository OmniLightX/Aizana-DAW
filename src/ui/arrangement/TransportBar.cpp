#include "TransportBar.h"

//==============================================================================

TransportBar::TransportBar (ApplicationState& appState_)
    : appState (appState_)
{
    // Style position label
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setText ("0:00:00.000", juce::dontSendNotification);
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    positionLabel.setColour (juce::Label::textColourId, juce::Colours::white);

    // Style BPM label
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::FontOptions (14.0f));
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    // Update BPM display from edit tempo
    if (auto* edit = appState.editManager.getEdit())
    {
        auto& tempoSeq = edit->tempoSequence;
        if (tempoSeq.getNumTempos() > 0)
        {
            auto bpm = tempoSeq.getTempo (0)->getBpm();
            bpmLabel.setText (juce::String (bpm, 1) + " BPM", juce::dontSendNotification);
        }
        else
        {
            bpmLabel.setText ("120.0 BPM", juce::dontSendNotification);
        }
    }

    // Style transport buttons
    playButton.setColour   (juce::TextButton::buttonColourId,     juce::Colour (0xff2d6a2d));
    playButton.setColour   (juce::TextButton::buttonOnColourId,   juce::Colour (0xff3a8f3a));
    stopButton.setColour   (juce::TextButton::buttonColourId,     juce::Colour (0xff3a3a3a));
    recordButton.setColour (juce::TextButton::buttonColourId,     juce::Colour (0xff8f2d2d));
    recordButton.setColour (juce::TextButton::buttonOnColourId,   juce::Colour (0xffcc3333));

    // Wire buttons
    playButton.onClick   = [this] { playClicked(); };
    stopButton.onClick   = [this] { stopClicked(); };
    recordButton.onClick = [this] { recordClicked(); };

    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (bpmLabel);

    // Start 30ms timer to update position display during playback
    startTimer (30);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (4);

    // Left side: transport buttons
    const int buttonW = 60;
    const int buttonH = area.getHeight();

    playButton.setBounds   (area.removeFromLeft (buttonW).withHeight (buttonH));
    area.removeFromLeft (4);
    stopButton.setBounds   (area.removeFromLeft (buttonW).withHeight (buttonH));
    area.removeFromLeft (4);
    recordButton.setBounds (area.removeFromLeft (buttonW).withHeight (buttonH));

    // Right side: BPM display
    bpmLabel.setBounds (area.removeFromRight (120));

    // Centre: position display
    positionLabel.setBounds (area);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Bottom border to visually separate from arrangement
    g.setColour (juce::Colour (0xff000000));
    g.drawLine (0.0f, (float)getHeight() - 1.0f, (float)getWidth(), (float)getHeight() - 1.0f, 1.0f);
}

void TransportBar::timerCallback()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    auto pos = transport.getPosition();

    // Format as M:SS:BB.mmm (bars not available without tempo analysis; use H:MM:SS.mmm)
    const double totalSeconds = pos.inSeconds();
    const int hours   = (int)(totalSeconds / 3600.0);
    const int minutes = (int)(std::fmod (totalSeconds, 3600.0) / 60.0);
    const int seconds = (int)std::fmod (totalSeconds, 60.0);
    const int millis  = (int)(std::fmod (totalSeconds, 1.0) * 1000.0);

    auto text = juce::String::formatted ("%d:%02d:%02d.%03d", hours, minutes, seconds, millis);
    positionLabel.setText (text, juce::dontSendNotification);

    // Toggle button states based on playback
    playButton.setToggleState (transport.isPlaying(), juce::dontSendNotification);
    recordButton.setToggleState (transport.isRecording(), juce::dontSendNotification);
}

void TransportBar::playClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    if (transport.isPlaying())
        transport.stop (false, false);
    else
        transport.play (false);
}

void TransportBar::stopClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    edit->getTransport().stop (false, false);

    // Return playhead to zero on second stop (Logic/Pro Tools behaviour)
    // First stop: halt at current position. Already handled by transport.stop().
}

void TransportBar::recordClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    if (transport.isRecording())
        transport.stop (false, false);
    else
        transport.record (false);
}
