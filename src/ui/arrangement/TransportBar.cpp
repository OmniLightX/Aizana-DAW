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

    // Make BPM label editable: double-click enters edit mode
    bpmLabel.setEditable (false, true, false);  // (singleClick, doubleClick, lossOfFocusDiscards)
    bpmLabel.onTextChange = [this]
    {
        auto newBpm = bpmLabel.getText().getDoubleValue();
        if (newBpm >= 20.0 && newBpm <= 300.0)
        {
            if (auto* edit = appState.editManager.getEdit())
            {
                auto& undoManager = appState.editManager.getUndoManager();
                undoManager.beginNewTransaction ("Set BPM");
                auto& seq = edit->tempoSequence;
                if (seq.getNumTempos() > 0)
                    seq.getTempo (0)->setBpm (newBpm);
            }
        }
        // Refresh label with the actually-applied value
        updateBpmLabel();
    };

    // Update BPM display from edit tempo
    updateBpmLabel();

    // Style sample rate label — brighter text + pointer cursor signals interactivity
    sampleRateLabel.setJustificationType (juce::Justification::centred);
    sampleRateLabel.setFont (juce::FontOptions (11.0f));
    sampleRateLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    sampleRateLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    sampleRateLabel.setText ("-- kHz", juce::dontSendNotification);

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
    addAndMakeVisible (sampleRateLabel);

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

    // Right side: sample rate + BPM display (rightmost to leftmost)
    sampleRateLabel.setBounds (area.removeFromRight (70));
    area.removeFromRight (4);  // gap
    bpmLabel.setBounds (area.removeFromRight (100));

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

    // Keep BPM label in sync (handles undo/redo changes)
    if (! bpmLabel.isBeingEdited())
        updateBpmLabel();

    // Sample rate update — every ~2 seconds (60 ticks at 30ms = 1.8s)
    if (++sampleRateUpdateCounter >= 60)
    {
        sampleRateUpdateCounter = 0;
        auto* device = appState.audioEngine.getDeviceManager().getCurrentAudioDevice();
        if (device != nullptr)
        {
            const double sr = device->getCurrentSampleRate();
            const juce::String srText = (sr >= 1000.0)
                ? juce::String (sr / 1000.0, 1) + " kHz"
                : juce::String (sr, 0) + " Hz";
            sampleRateLabel.setText (srText, juce::dontSendNotification);
        }
    }
}

void TransportBar::updateBpmLabel()
{
    if (auto* edit = appState.editManager.getEdit())
    {
        auto& seq = edit->tempoSequence;
        if (seq.getNumTempos() > 0)
        {
            const double bpm = seq.getTempo (0)->getBpm();
            bpmLabel.setText (juce::String (bpm, 1) + " BPM", juce::dontSendNotification);
        }
    }
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

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    // Only respond to clicks on the sample rate label
    if (sampleRateLabel.getBounds().contains (e.getPosition()))
        showSampleRateMenu();
}

void TransportBar::showSampleRateMenu()
{
    auto& deviceManager = appState.audioEngine.getDeviceManager();
    auto* device = deviceManager.getCurrentAudioDevice();
    const double currentRate = (device != nullptr) ? device->getCurrentSampleRate() : 0.0;

    // Standard professional rates only — clean popup like iOS Settings
    // Apple approach: present only the meaningful choices, no technical noise
    struct RateOption { double rate; const char* label; };
    const RateOption options[] = {
        { 44100.0, "44.1 kHz" },
        { 48000.0, "48 kHz"   },
        { 88200.0, "88.2 kHz" },
        { 96000.0, "96 kHz"   },
    };

    juce::PopupMenu menu;
    int id = 1;
    for (const auto& opt : options)
    {
        const bool isCurrent = std::abs (opt.rate - currentRate) < 1.0;
        menu.addItem (id++, opt.label, true, isCurrent);
    }

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
            sampleRateLabel.getScreenBounds()),
        [this] (int result)
        {
            if (result < 1 || result > 4) return;

            const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
            const double chosenRate = rates[result - 1];

            auto& deviceManager = appState.audioEngine.getDeviceManager();
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.sampleRate = chosenRate;
            deviceManager.setAudioDeviceSetup (setup, true);

            // Update label immediately — timerCallback will sync in 2s otherwise
            const juce::String srText = juce::String (chosenRate / 1000.0, 1) + " kHz";
            sampleRateLabel.setText (srText, juce::dontSendNotification);
        });
}
