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
    // Runder Enable-Punkt — grün = Send aktiv, grau = aus (Farbe malt paint()).
    dotButton.setClickingTogglesState (false);
    dotButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    dotButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    dotButton.setTooltip (juce::String::fromUTF8 ("Send an/aus"));
    dotButton.onClick = [this]
    {
        if (! sendEnabled())
        {
            if (! sendExists())
                mixerEngine.createFXSend (sourceTrack, fxChannel, shownLevelDb);
            else
                mixerEngine.setFXSendEnabled (sourceTrack, fxChannel, true);
        }
        else
        {
            mixerEngine.setFXSendEnabled (sourceTrack, fxChannel, false);
        }
        repaint();
    };
    addAndMakeVisible (dotButton);

    const float currentLevel = mixerEngine.getFXSendLevel (sourceTrack, fxChannel);
    shownLevelDb = currentLevel > -99.0f ? currentLevel : -6.0f;

    setTooltip (juce::String::fromUTF8 ("Ziehen = Send-Level (Shift = fein) \xc2\xb7 "
                                        "Doppelklick = -6 dB"));
}

bool SendSlotPanel::SendRow::sendExists() const
{
    return mixerEngine.getFXSendLevel (sourceTrack, fxChannel) > -99.0f;
}

bool SendSlotPanel::SendRow::sendEnabled() const
{
    return sendExists() && mixerEngine.isFXSendEnabled (sourceTrack, fxChannel);
}

float SendSlotPanel::SendRow::levelToNorm (float dB) const
{
    return juce::jlimit (0.0f, 1.0f, (dB + 60.0f) / 60.0f);
}

void SendSlotPanel::SendRow::applyLevel (float dB, bool beginTransaction)
{
    dB = juce::jlimit (-60.0f, 0.0f, dB);
    shownLevelDb = dB;

    // Eine Drag-Geste = EIN Undo-Schritt: nur die erste Bewegung öffnet die
    // Transaktion, die setFXSendLevel-Calls danach falten hinein.
    if (beginTransaction)
        sourceTrack.edit.getUndoManager().beginNewTransaction ("Send level");

    if (! sendExists())
        mixerEngine.createFXSend (sourceTrack, fxChannel, dB);
    else
        mixerEngine.setFXSendLevel (sourceTrack, fxChannel, dB);
    repaint();
}

void SendSlotPanel::SendRow::mouseDown (const juce::MouseEvent& e)
{
    dragTxnOpen = false;
    dragStartDb = shownLevelDb;
    dragStartX  = e.position.x;
}

void SendSlotPanel::SendRow::mouseDrag (const juce::MouseEvent& e)
{
    const float barW = (float) juce::jmax (1, getWidth() - kDotW);
    const float dx   = e.position.x - dragStartX;
    if (! dragTxnOpen && std::abs (dx) < 2.0f)
        return;

    // Horizontal: volle Breite = 60 dB; Shift = Feinmodus (0.1 dB/px).
    const double delta = e.mods.isShiftDown() ? (double) dx * 0.1
                                              : (double) dx / barW * 60.0;
    applyLevel ((float) (dragStartDb + delta), ! dragTxnOpen);
    dragTxnOpen = true;
}

void SendSlotPanel::SendRow::mouseDoubleClick (const juce::MouseEvent&)
{
    applyLevel (-6.0f, true);
}

void SendSlotPanel::SendRow::paint (juce::Graphics& g)
{
    const bool exists  = sendExists();
    const bool enabled = sendEnabled();
    if (exists)
        shownLevelDb = mixerEngine.getFXSendLevel (sourceTrack, fxChannel);

    auto area = getLocalBounds();
    area.removeFromBottom (1);   // Separator-Zeile

    // Enable-Punkt (rund, S1-Stil)
    {
        auto dot = area.removeFromLeft (kDotW).toFloat();
        const float r = 4.5f;
        g.setColour (enabled ? juce::Colour (0xff33cc55) : juce::Colour (0xff3a3a42));
        g.fillEllipse (dot.getCentreX() - r, dot.getCentreY() - r, r * 2.0f, r * 2.0f);
        if (enabled)
        {
            g.setColour (juce::Colour (0xff33cc55).withAlpha (0.35f));
            g.drawEllipse (dot.getCentreX() - r - 1.5f, dot.getCentreY() - r - 1.5f,
                           (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f, 1.0f);
        }
    }

    // Level-Balken: die ganze Restzeile ist die Skala (-60..0 dB) — satter Fill
    // statt des alten halbtransparenten Mini-Sliders.
    auto bar = area.toFloat().reduced (0.0f, 2.0f);
    g.setColour (juce::Colour (0xff26262c));
    g.fillRoundedRectangle (bar, 3.0f);

    const float fillW = bar.getWidth() * levelToNorm (shownLevelDb);
    if (fillW > 1.0f)
    {
        g.setColour (enabled ? juce::Colour (0xff2f6ea8) : juce::Colour (0xff3a4450));
        g.fillRoundedRectangle (bar.withWidth (fillW), 3.0f);
    }

    // FX-Name links AUF dem Balken, dB-Wert rechts.
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (enabled ? juce::Colours::white : juce::Colour (0xff9999a5));
    g.drawText (fxChannel.getName(),
                bar.toNearestInt().withTrimmedLeft (6).withTrimmedRight (46),
                juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (enabled ? juce::Colours::white.withAlpha (0.95f)
                         : juce::Colour (0xff88888f));
        g.drawText (exists ? juce::String (shownLevelDb, 1) + " dB"
                       : juce::String::fromUTF8 ("\xe2\x80\x93"),
                bar.toNearestInt().withTrimmedRight (4),
                juce::Justification::centredRight, true);

    g.setColour (juce::Colour (0xff111111));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void SendSlotPanel::SendRow::resized()
{
    dotButton.setBounds (getLocalBounds().removeFromLeft (kDotW));
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
    sendTargets = fxChannels;   // "+ Send"-Menü bietet weiterhin ALLE FX-Busse an

    for (auto* fxCh : fxChannels)
    {
        // S1-Verhalten (User-Befund 2026-07-06): Zeilen NUR für Sends, die auf
        // DIESEM Kanal existieren. Vorher bekam jeder Strip eine (graue) Zeile
        // pro FX-Bus — ein neuer Delay-Send auf dem Reverb-Bus "erschien"
        // dadurch scheinbar auf allen Vocal-Spuren.
        if (mixerEngine.getFXSendLevel (track, *fxCh) <= -99.0f)
            continue;

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
