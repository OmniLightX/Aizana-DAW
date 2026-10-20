#include "VCAStrip.h"

namespace
{
    const juce::Colour kVcaAccent   (0xffc8a030);   // gold-amber — distinct from Bus blue / FX purple / Master gold
    const juce::Colour kStripBg     (0xff202020);
    const juce::Colour kSelectedBg  (0xff2a2a34);
    const juce::Colour kBorder      (0xff3a3a3a);
}

//==============================================================================
VCAStrip::VCAStrip (te::AudioTrack& vcaTrack, MixerEngine& me)
    : track (vcaTrack), mixerEngine (me)
{
    nameLabel.setText (track.getName(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nameLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    nameLabel.setEditable (false, true, false);
    nameLabel.onTextChange = [this]
    {
        track.edit.getUndoManager().beginNewTransaction ("Rename VCA");
        track.setName (nameLabel.getText());
    };
    addAndMakeVisible (nameLabel);

    membersButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff333333));
    membersButton.setTooltip (juce::String::fromUTF8 ("Tracks diesem VCA zuweisen / entfernen"));
    membersButton.onClick = [this] { showMembersMenu(); };
    addAndMakeVisible (membersButton);

    for (auto* b : { &soloButton, &muteButton })
    {
        b->setClickingTogglesState (false);   // invariant I-04
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff333333));
        addAndMakeVisible (*b);
    }
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::yellow.darker (0.2f));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::red.darker (0.2f));
    soloButton.setTooltip (juce::String::fromUTF8 ("Solo für alle VCA-Mitglieder"));
    muteButton.setTooltip (juce::String::fromUTF8 ("Mute für alle VCA-Mitglieder"));

    // S/M apply the VCA track's own state to all members (one transaction each)
    soloButton.onClick = [this]
    {
        const bool newState = ! track.isSolo (false);
        mixerEngine.setSolo (track, newState);
        for (auto* m : mixerEngine.getVCAMembers (track))
            mixerEngine.setSolo (*m, newState);
        refresh();
        if (onMembersChanged) onMembersChanged();
    };
    muteButton.onClick = [this]
    {
        const bool newState = ! track.isMuted (false);
        mixerEngine.setMute (track, newState);
        for (auto* m : mixerEngine.getVCAMembers (track))
            mixerEngine.setMute (*m, newState);
        refresh();
        if (onMembersChanged) onMembersChanged();
    };

    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setTextValueSuffix (" dB");
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setColour (juce::Slider::thumbColourId, kVcaAccent);
    fader.setValue (mixerEngine.getVolume (track), juce::dontSendNotification);
    fader.onDragStart   = [this] { beginVcaDrag(); };
    fader.onValueChange = [this] { applyVcaDrag (fader.getValue()); };
    fader.onDragEnd     = [this] { memberStartDb.clear(); };
    addAndMakeVisible (fader);
    for (auto* child : getChildren()) child->addMouseListener(this, true);
}

//==============================================================================
void VCAStrip::beginVcaDrag()
{
    dragStartDb = mixerEngine.getVolume (track);
    memberStartDb.clear();
    for (auto* m : mixerEngine.getVCAMembers (track))
        memberStartDb.emplace_back (m, (double) mixerEngine.getVolume (*m));
}

void VCAStrip::applyVcaDrag (double newDb)
{
    // Store the VCA's own fader position (persisted on its VolumeAndPanPlugin)…
    mixerEngine.setVolume (track, (float) newDb);

    if (memberStartDb.empty())
        return;   // double-click reset or programmatic set without drag

    // …and move every member by the same dB delta, preserving balance.
    const double delta = newDb - dragStartDb;
    for (auto& [member, startDb] : memberStartDb)
        mixerEngine.setVolume (*member, (float) juce::jlimit (-60.0, 6.0, startDb + delta));

    if (onMembersChanged) onMembersChanged();
}

void VCAStrip::showMembersMenu()
{
    auto& edit = track.edit;
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 ("VCA-Mitglieder"));

    juce::Array<te::AudioTrack*> candidates;
    for (auto* t : te::getAudioTracks (edit))
        if (MixerEngine::isAudioChannel (*t))
            candidates.add (t);

    for (int i = 0; i < candidates.size(); ++i)
    {
        auto* t = candidates[i];
        const bool isMember = (mixerEngine.getVCAFor (*t) == &track);
        menu.addItem (100 + i, t->getName(), true, isMember);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&membersButton),
        [this, candidates] (int result)
        {
            if (result < 100)
                return;
            auto* t = candidates[result - 100];
            if (t == nullptr)
                return;
            if (mixerEngine.getVCAFor (*t) == &track)
                mixerEngine.unassignFromVCA (*t);
            else
                mixerEngine.assignTrackToVCA (*t, track);
            repaint();
        });
}

//==============================================================================
void VCAStrip::refresh()
{
    soloButton.setToggleState (track.isSolo (false), juce::dontSendNotification);
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    if (! fader.isMouseButtonDown())
        fader.setValue (mixerEngine.getVolume (track), juce::dontSendNotification);
    nameLabel.setText (track.getName(), juce::dontSendNotification);
}

void VCAStrip::setSelected (bool s)
{
    isSelected = s;
    repaint();
}

void VCAStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (e.eventComponent == this || e.eventComponent == &nameLabel)
    {
        if (onTrackSelected) onTrackSelected(&track);
    }
    else if (onTrackControlTouched)
        onTrackControlTouched(&track);
}

void VCAStrip::paint (juce::Graphics& g)
{
    g.fillAll (isSelected ? kSelectedBg : kStripBg);

    // Accent bar at top (matches Bus/FX strip pattern)
    g.setColour (kVcaAccent);
    g.fillRect (0, 0, getWidth(), 3);

    g.setColour (kBorder);
    g.drawRect (getLocalBounds(), 1);

    // Member count under the assign button
    const int n = mixerEngine.getVCAMembers (track).size();
    g.setColour (juce::Colour (0xff888888));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (juce::String (n) + (n == 1 ? " Track" : " Tracks"),
                getLocalBounds().withTrimmedTop (78).withHeight (14),
                juce::Justification::centred, false);
}

void VCAStrip::resized()
{
    auto area = getLocalBounds().reduced (6);
    area.removeFromTop (6);                       // accent bar
    nameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);
    membersButton.setBounds (area.removeFromTop (20));
    area.removeFromTop (18);                      // member count text (painted)

    auto smRow = area.removeFromTop (22);
    soloButton.setBounds (smRow.removeFromLeft (smRow.getWidth() / 2).reduced (2, 0));
    muteButton.setBounds (smRow.reduced (2, 0));
    area.removeFromTop (6);

    fader.setBounds (area);
}
