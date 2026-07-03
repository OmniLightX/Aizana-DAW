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

    // FX channel name label — 10.5pt, brightens when the send is active
    // (live-test finding 2026-07-03: sends were barely readable at 8.5pt)
    nameLabel.setText (fxChannel.getName(), juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (10.5f));
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
                         enabled ? juce::Colour (0xff33cc33) : juce::Colour (0xff3a3a3a));
    nameLabel.setColour (juce::Label::textColourId,
                         enabled ? juce::Colours::white : juce::Colour (0xffaaaaaa));
    dotButton.repaint();
    nameLabel.repaint();
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

    const int kDotW  = 14;
    const int kNameW = 56;

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

    // "+ Send" — Studio One pattern: pick an FX bus (or create a new one).
    addSendButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff222228));
    addSendButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9999aa));
    addSendButton.onClick = [this] { showAddSendMenu(); };
    content.addAndMakeVisible (addSendButton);
}

void SendSlotPanel::rebuild (const juce::Array<te::AudioTrack*>& fxChannels)
{
    rows.clear();
    sendTargets = fxChannels;

    for (auto* fxCh : fxChannels)
    {
        auto* row = rows.add (new SendRow (track, *fxCh, mixerEngine));
        content.addAndMakeVisible (row);
    }

    addSendButton.toFront (false);
    resized();
    repaint();
}

void SendSlotPanel::showAddSendMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Send auf FX-Bus");

    for (int i = 0; i < sendTargets.size(); ++i)
        menu.addItem (i + 1, sendTargets[i]->getName());

    if (! sendTargets.isEmpty())
        menu.addSeparator();
    const int newFxId = sendTargets.size() + 1;
    menu.addItem (newFxId, juce::String::fromUTF8 ("Neuer FX-Channel\xe2\x80\xa6"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSendButton),
        [this, newFxId] (int result)
        {
            if (result <= 0)
                return;

            if (result == newFxId)
            {
                promptNewFxChannel();
                return;
            }

            if (auto* fx = sendTargets[result - 1])
            {
                if (mixerEngine.getFXSendLevel (track, *fx) <= -99.0f)
                    mixerEngine.createFXSend (track, *fx, -6.0f);
                else
                    mixerEngine.setFXSendEnabled (track, *fx, true);
                rebuild (sendTargets);   // reflect the new/enabled send immediately
            }
        });
}

void SendSlotPanel::promptNewFxChannel()
{
    auto* w = new juce::AlertWindow ("Neuer FX-Channel",
                                     juce::String::fromUTF8 ("Name des FX-Busses (z. B. Reverb, Delay):"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "FX " + juce::String (sendTargets.size() + 1));
    w->addButton ("Erstellen", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<SendSlotPanel> safe (this);
    w->enterModalState (true,
        juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (w);
            if (safe == nullptr || result != 1)
                return;

            const auto name = w->getTextEditorContents ("name").trim();
            if (name.isEmpty())
                return;

            // Create bus + send now; the MixerView rebuilds all strips/sends
            // asynchronously via its ValueTree listener (track added).
            if (auto* fx = safe->mixerEngine.createFXChannel (name))
                safe->mixerEngine.createFXSend (safe->track, *fx, -6.0f);
        }),
        false);
}

void SendSlotPanel::resized()
{
    viewport.setBounds (getLocalBounds());

    const int contentW = juce::jmax (1, getWidth());
    const int contentH = (rows.size() + 1) * kRowHeight;   // +1 = "+ Send" row
    content.setSize (contentW, juce::jmax (kRowHeight, contentH));

    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * kRowHeight, contentW, kRowHeight);

    addSendButton.setBounds (2, rows.size() * kRowHeight + 1, contentW - 4, kRowHeight - 2);
}

void SendSlotPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
}
