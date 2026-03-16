#include "ChannelStrip.h"

//==============================================================================
// MeterBar

MeterBar::MeterBar (MeterProcessor& meterProcessor_)
    : meterProcessor (meterProcessor_)
{
    // LUFS-I label — below the meter bar
    // REQUIREMENTS.md + CONTEXT.md: informational only, no target enforcement
    lufsLabel.setText ("LUFS-I", juce::dontSendNotification);
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    lufsLabel.setFont (juce::FontOptions (8.0f));
    addAndMakeVisible (lufsLabel);

    startTimerHz (24);  // ~24fps — matches EXP-03 requirement
}

MeterBar::~MeterBar()
{
    stopTimer();
}

void MeterBar::timerCallback()
{
    // Read atomic values — thread-safe, no lock needed
    const float newPeak = meterProcessor.getPeakDB();
    const float newRMS  = meterProcessor.getRMSDB();
    const float newLUFS = meterProcessor.getLUFSIntegrated();

    bool needsRepaint = false;

    if (std::abs (newPeak - displayPeakDB) > 0.1f)
    {
        displayPeakDB = newPeak;
        needsRepaint = true;
    }

    if (std::abs (newRMS - displayRMSDB) > 0.1f)
    {
        displayRMSDB = newRMS;
        needsRepaint = true;
    }

    // Peak hold logic
    if (displayPeakDB > peakHoldDB)
    {
        peakHoldDB    = displayPeakDB;
        peakHoldTicks = kPeakHoldTicks;
        needsRepaint  = true;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
        if (peakHoldTicks == 0)
        {
            peakHoldDB   = -100.0f;
            needsRepaint = true;
        }
    }

    if (std::abs (newLUFS - displayLUFSI) > 0.1f)
    {
        displayLUFSI = newLUFS;

        // Update LUFS label — informational, neutral colour
        const juce::String lufsText = displayLUFSI <= -99.0f
            ? "---"
            : juce::String (displayLUFSI, 1);
        lufsLabel.setText (lufsText, juce::dontSendNotification);
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void MeterBar::setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI)
{
    // Called from MixerView::timerCallback (message thread) with master meter values.
    // Updates display state and triggers a repaint if any value changed significantly.
    bool needsRepaint = false;

    if (std::abs (peakDB - displayPeakDB) > 0.1f)
    {
        displayPeakDB = peakDB;
        needsRepaint  = true;
    }

    if (std::abs (rmsDB - displayRMSDB) > 0.1f)
    {
        displayRMSDB = rmsDB;
        needsRepaint = true;
    }

    if (std::abs (lufsM - displayLUFSM) > 0.1f)
    {
        displayLUFSM = lufsM;
        needsRepaint = true;
    }

    if (std::abs (lufsI - displayLUFSI) > 0.1f)
    {
        displayLUFSI = lufsI;

        const juce::String lufsText = displayLUFSI <= -99.0f
            ? "---"
            : juce::String (displayLUFSI, 1);
        lufsLabel.setText (lufsText, juce::dontSendNotification);
        needsRepaint = true;
    }

    // Peak hold logic — driven by pushed values (not read from processor)
    if (displayPeakDB > peakHoldDB)
    {
        peakHoldDB    = displayPeakDB;
        peakHoldTicks = kPeakHoldTicks;
        needsRepaint  = true;
    }
    else if (peakHoldTicks > 0)
    {
        --peakHoldTicks;
        if (peakHoldTicks == 0)
        {
            peakHoldDB   = -100.0f;
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        repaint();
}

float MeterBar::dbToNorm (float dB)
{
    // -60dBFS at bottom, 0dBFS at top. Clamp below -60.
    return juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 60.0f);
}

void MeterBar::resized()
{
    // LUFS label at the very bottom
    lufsLabel.setBounds (getLocalBounds().removeFromBottom (14));
}

void MeterBar::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    // Reserve bottom 14px for LUFS label
    area.removeFromBottom (14.0f);

    const float barW = area.getWidth() * 0.45f;
    const float barX = (area.getWidth() - barW * 2.0f - 2.0f) * 0.5f + area.getX();

    // Background track
    g.setColour (juce::Colour (0xff111111));
    g.fillRect (area);

    // ---- Peak bar (left half) ----
    {
        const float norm = dbToNorm (displayPeakDB);
        const float barH = norm * area.getHeight();
        const float barY = area.getBottom() - barH;

        // Colour gradient: green → yellow → red
        juce::ColourGradient grad;
        grad.isRadial = false;
        grad.point1 = { barX, area.getBottom() };
        grad.point2 = { barX, area.getY() };
        grad.addColour (0.0, juce::Colour (0xff00aa44));
        grad.addColour (0.7, juce::Colour (0xffddcc00));
        grad.addColour (0.9, juce::Colour (0xffff4400));
        grad.addColour (1.0, juce::Colour (0xffff0000));

        g.setGradientFill (grad);
        g.fillRect (barX, barY, barW, barH);

        // Peak hold line
        if (peakHoldDB > -99.0f)
        {
            const float holdNorm = dbToNorm (peakHoldDB);
            const float holdY = area.getBottom() - holdNorm * area.getHeight();
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawHorizontalLine ((int)holdY, barX, barX + barW);
        }
    }

    // ---- RMS bar (right half) ----
    {
        const float rmsNorm = dbToNorm (displayRMSDB);
        const float rmsH    = rmsNorm * area.getHeight();
        const float rmsY    = area.getBottom() - rmsH;
        const float rmsX    = barX + barW + 2.0f;

        g.setColour (juce::Colour (0xff226688));
        g.fillRect (rmsX, rmsY, barW, rmsH);
    }

    // Tick marks at -6, -12, -18, -24 dBFS with dB scale labels
    for (float dB : { -6.0f, -12.0f, -18.0f, -24.0f })
    {
        const float norm  = dbToNorm (dB);
        const float tickY = area.getBottom() - norm * area.getHeight();

        // Tick line
        g.setColour (juce::Colour (0xff444444));
        g.drawHorizontalLine ((int)tickY, area.getX(), area.getRight());

        // dB text label — drawn inside the meter bar with semi-transparent colour
        // so it's visible at any strip width without needing external space
        g.setFont (juce::FontOptions (8.0f));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawText (juce::String (static_cast<int> (dB)),
                    (int)area.getX(),
                    (int)(tickY - 9.0f),  // above the tick line
                    (int)area.getWidth(),
                    9,
                    juce::Justification::centredRight,
                    false);
    }
}

//==============================================================================
// ChannelStrip

ChannelStrip::ChannelStrip (te::AudioTrack& track_, MixerEngine& mixerEngine_,
                             MeterProcessor& meterProcessor_)
    : track (track_)
    , mixerEngine (mixerEngine_)
    , meterBar (meterProcessor_)
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
    panKnob.setTextBoxStyle    (juce::Slider::NoTextBox, false, 0, 0);
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

    // ---- Pan label ----
    panLabel.setText ("PAN", juce::dontSendNotification);
    panLabel.setJustificationType (juce::Justification::centred);
    panLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    panLabel.setFont (juce::FontOptions (9.0f));

    // ---- Track name label ----
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setJustificationType (juce::Justification::centred);
    trackNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    trackNameLabel.setFont (juce::FontOptions (10.0f));

    addAndMakeVisible (meterBar);
    addAndMakeVisible (volumeFader);
    addAndMakeVisible (panKnob);
    addAndMakeVisible (panLabel);
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

    // Without text box, the full height is the rotary dial itself — use 55px to make it prominent
    const int panH = juce::jmax (48, juce::jmin (60, area.getHeight() / 4));
    panKnob.setBounds (area.removeFromTop (panH));
    panLabel.setBounds (area.removeFromTop (12));

    // Meter bar on the left ~30% width, volume fader on the right ~70%
    const int meterW = juce::jmax (16, area.getWidth() * 30 / 100);
    meterBar.setBounds (area.removeFromLeft (meterW));

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

void ChannelStrip::setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI)
{
    // Delegate to the MeterBar — all display logic lives there.
    meterBar.setMeterValues (peakDB, rmsDB, lufsM, lufsI);
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
    if (property == te::IDs::volume)
    {
        const float dB = mixerEngine.getVolume (track);
        volumeFader.setValue (dB, juce::dontSendNotification);
    }
}
