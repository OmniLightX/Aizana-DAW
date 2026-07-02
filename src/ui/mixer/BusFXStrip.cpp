#include "BusFXStrip.h"
#include "plugins/BuiltInPluginFormat.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"

//==============================================================================
// MiniMeter

float BusFXStrip::MiniMeter::dbToNorm (float db)
{
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

void BusFXStrip::MiniMeter::setValues (float peak)
{
    if (peak > holdDB)
    {
        holdDB  = peak;
        holdAge = 0;
    }
    else
    {
        ++holdAge;
        if (holdAge > kHoldTicks)
            holdDB = juce::jmax (holdDB - 0.5f, peak);
    }

    peakDB = peak;
    repaint();
}

void BusFXStrip::MiniMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff111111));
    g.fillRect (area);

    const float norm = dbToNorm (peakDB);
    const float barH = norm * area.getHeight();
    const auto  barRect = area.withTop (area.getBottom() - barH);

    if (barH > 0.0f)
    {
        juce::ColourGradient grad (juce::Colour (0xff00aa44), barRect.getBottomLeft(),
                                   juce::Colour (0xffff4400), barRect.getTopLeft(), false);
        grad.addColour (0.7, juce::Colour (0xffddcc00));
        g.setGradientFill (grad);
        g.fillRect (barRect);
    }

    if (holdDB > -99.0f)
    {
        const float holdY = area.getBottom() - area.getHeight() * dbToNorm (holdDB);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawHorizontalLine (juce::roundToInt (holdY), area.getX(), area.getRight());
    }

    g.setColour (juce::Colour (0xff333333));
    g.drawRect (area, 1);
}

//==============================================================================
// InsertSlotPanel::SlotButton

void BusFXStrip::InsertSlotPanel::SlotButton::paint (juce::Graphics& g)
{
    const bool occupied = (plugin != nullptr);
    const bool bypassed = occupied && !plugin->isEnabled();
    g.fillAll (occupied ? (bypassed ? juce::Colour (0xff1e1e1e) : juce::Colour (0xff2a2a2a))
                        : juce::Colour (0xff1a1a1a));

    g.setColour (occupied ? (bypassed ? juce::Colour (0xff444444) : juce::Colour (0xff666666))
                           : juce::Colour (0xff444444));
    g.drawRect (getLocalBounds(), 1);

    // 11pt — plugin names must be readable at a glance (Studio One size); 8pt was too small
    g.setFont (juce::FontOptions (11.0f));

    if (occupied)
    {
        const int dotSize = 6;
        const int dotX = getWidth() - dotSize - 3;
        const int dotY = (getHeight() - dotSize) / 2;
        g.setColour (bypassed ? juce::Colour (0xff555555) : juce::Colour (0xff44cc44));
        g.fillEllipse ((float)dotX, (float)dotY, (float)dotSize, (float)dotSize);

        g.setColour (bypassed ? juce::Colour (0xff666666) : juce::Colours::white);
        g.drawText (plugin->getName(),
                    getLocalBounds().withTrimmedRight (dotSize + 6).reduced (2, 1),
                    juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (juce::Colour (0xff888888));
        g.drawText ("+", getLocalBounds(), juce::Justification::centred, false);
    }
}

void BusFXStrip::InsertSlotPanel::SlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return;
    }

    if (plugin != nullptr && e.x >= getWidth() - 12)
    {
        plugin->setEnabled (!plugin->isEnabled());
        repaint();
        return;
    }

    if (onClick)
        onClick();
}

void BusFXStrip::InsertSlotPanel::SlotButton::mouseDrag (const juce::MouseEvent& e)
{
    if (plugin == nullptr)
        return;   // empty "+" slot cannot be dragged

    if (! dragging && e.getDistanceFromDragStart() < 5)
        return;

    dragging = true;
    if (onDragMove)
        onDragMove (e);
}

void BusFXStrip::InsertSlotPanel::SlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        if (onDragEnd)
            onDragEnd (e);
    }
}

void BusFXStrip::InsertSlotPanel::SlotButton::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

//==============================================================================
// InsertSlotPanel

BusFXStrip::InsertSlotPanel::InsertSlotPanel (te::AudioTrack& track_,
                                               PluginManager& pm,
                                               PluginWindowManager& pwm,
                                               PluginScanner& ps)
    : track (track_)
    , pluginManager (pm)
    , pluginWindowManager (pwm)
    , pluginScanner (ps)
{
    slotsViewport.setViewedComponent (&slotsContent, false);
    slotsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (slotsViewport);

    refresh();
}

void BusFXStrip::InsertSlotPanel::resized()
{
    slotsViewport.setBounds (getLocalBounds());

    const int contentW = juce::jmax (1, getWidth());
    const int contentH = juce::jmax (kSlotHeight, slots.size() * kSlotHeight);
    slotsContent.setSize (contentW, contentH);
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setBounds (0, i * kSlotHeight, contentW, kSlotHeight);
}

void BusFXStrip::InsertSlotPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
}

void BusFXStrip::InsertSlotPanel::paintOverChildren (juce::Graphics& g)
{
    // Insertion indicator while drag-reordering: accent line at the target slot's top edge
    if (dragTargetSlot < 0)
        return;

    const auto topInContent = juce::Point<int> (0, dragTargetSlot * kSlotHeight);
    const auto topInPanel   = getLocalPoint (&slotsContent, topInContent);

    g.setColour (juce::Colour (0xff44aaff));
    g.fillRect (0, topInPanel.y - 1, getWidth(), 2);
}

int BusFXStrip::InsertSlotPanel::slotIndexForEvent (const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (&slotsContent).getPosition();
    return juce::jlimit (0, juce::jmax (0, slots.size() - 1), pos.y / kSlotHeight);
}

void BusFXStrip::InsertSlotPanel::commitSlotDrag (int fromSlot, int toSlot)
{
    if (fromSlot != toSlot)
        pluginManager.moveUserPluginToSlot (track, fromSlot, toSlot);

    refresh();
}

void BusFXStrip::InsertSlotPanel::refresh()
{
    slots.clear();

    const auto chain = pluginManager.getPluginChain (track);
    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : chain)
    {
        const juce::String t = p->getPluginType();
        if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
            userPlugins.add (p);
    }

    const int numSlots = juce::jmax (1, userPlugins.size() + 1);
    const int contentW = juce::jmax (1, getWidth());

    for (int i = 0; i < numSlots; ++i)
    {
        auto* slot = slots.add (new SlotButton());
        if (i < userPlugins.size())
            slot->plugin = userPlugins[i];

        const int slotIndex = i;

        slot->onClick = [this, slotIndex]
        {
            if (slotIndex < slots.size() && slots[slotIndex]->plugin == nullptr)
                showAddPluginMenu (slotIndex);
        };
        slot->onDoubleClick = [this, slotIndex]
        {
            if (slotIndex < slots.size() && slots[slotIndex]->plugin != nullptr)
                pluginWindowManager.openPluginWindow (*slots[slotIndex]->plugin);
        };
        slot->onRightClick = [this, slotIndex]
        {
            if (slotIndex < slots.size() && slots[slotIndex]->plugin != nullptr)
                showRemovePluginMenu (slots[slotIndex]->plugin);
        };

        // Drag-reorder: track the hovered slot while dragging, commit on release.
        slot->onDragMove = [this] (const juce::MouseEvent& e)
        {
            dragTargetSlot = slotIndexForEvent (e);
            repaint();
        };
        slot->onDragEnd = [this, slotIndex] (const juce::MouseEvent& e)
        {
            const int target = slotIndexForEvent (e);
            dragTargetSlot = -1;
            repaint();

            // Deferred: commitSlotDrag() → moveUserPluginToSlot() → ValueTree
            // listener → refresh() destroys the SlotButton whose mouseUp is
            // still on the stack. callAsync + SafePointer avoids the UAF.
            juce::Component::SafePointer<InsertSlotPanel> safe (this);
            juce::MessageManager::callAsync ([safe, slotIndex, target]
            {
                if (safe == nullptr)
                    return;
                safe->commitSlotDrag (slotIndex, target);
            });
        };

        slot->setBounds (0, i * kSlotHeight, contentW, kSlotHeight);
        slotsContent.addAndMakeVisible (slot);
    }

    slotsContent.setSize (contentW, numSlots * kSlotHeight);
    repaint();
}

void BusFXStrip::InsertSlotPanel::showAddPluginMenu (int slotIndex)
{
    // Map the user-visible slot index to the raw chain index. Internal TE
    // plugins (volume/level/auxreturn/auxsend) can sit anywhere in the raw
    // chain — a simple prefix offset placed new plugins between existing user
    // plugins (live-test finding M3). PluginManager owns the correct mapping.
    const int actualIndex = pluginManager.chainInsertIndexForUserSlot (track, slotIndex);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Built-in FX");

    juce::OwnedArray<juce::PluginDescription> builtIns;
    BuiltInPluginFormat::getBuiltInDescriptions (builtIns);

    int itemId = 1;
    juce::Array<juce::PluginDescription> descs;
    for (auto* desc : builtIns)
    {
        descs.add (*desc);
        menu.addItem (itemId++, desc->name);
    }

    menu.addSeparator();
    menu.addItem (itemId, "Browse Plugins...");
    const int browseId = itemId;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, actualIndex, descs, browseId] (int result)
        {
            if (result <= 0)
                return;

            if (result == browseId)
            {
                PluginBrowserPopup::showForComponent (
                    *this,
                    pluginScanner.getKnownPlugins(),
                    [this, actualIndex] (const juce::PluginDescription& desc)
                    {
                        auto* newPlugin = pluginManager.insertPlugin (track, desc, actualIndex);
                        if (newPlugin != nullptr)
                        {
                            pluginWindowManager.openPluginWindow (*newPlugin);
                            refresh();
                        }
                    });
            }
            else
            {
                const int descIdx = result - 1;
                if (descIdx >= 0 && descIdx < descs.size())
                {
                    auto* newPlugin = pluginManager.insertPlugin (track, descs[descIdx], actualIndex);
                    if (newPlugin != nullptr)
                    {
                        pluginWindowManager.openPluginWindow (*newPlugin);
                        refresh();
                    }
                }
            }
        });
}

void BusFXStrip::InsertSlotPanel::showRemovePluginMenu (te::Plugin* plugin)
{
    if (plugin == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Remove \"" + plugin->getName() + "\"");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, plugin] (int result)
        {
            if (result == 1)
            {
                pluginManager.removePlugin (track, *plugin);
                refresh();
            }
        });
}

//==============================================================================
// BusFXStrip

BusFXStrip::BusFXStrip (te::AudioTrack& track_,
                         MixerEngine& mixerEngine_,
                         MeterProcessor& meterProcessor_,
                         PluginManager& pluginManager_,
                         PluginWindowManager& pluginWindowManager_,
                         PluginScanner& pluginScanner_)
    : track (track_)
    , mixerEngine (mixerEngine_)
    , pluginManager (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
    , insertSlotPanel (track_, pluginManager_, pluginWindowManager_, pluginScanner_)
{
    juce::ignoreUnused (meterProcessor_);  // meter values pushed via setMeterValues()

    volumeFader.setSliderStyle    (juce::Slider::LinearVertical);
    volumeFader.setTextBoxStyle   (juce::Slider::TextBoxBelow, false, 55, 16);
    volumeFader.setRange          (-60.0, 6.0, 0.1);
    volumeFader.setDoubleClickReturnValue (true, 0.0);
    volumeFader.setTextValueSuffix (" dB");
    volumeFader.setValue          (mixerEngine.getVolume (track), juce::dontSendNotification);
    volumeFader.onValueChange = [this] { volumeChanged(); };

    // ---- Solo / Mute buttons — same style as ChannelStrip ----
    soloButton.setClickingTogglesState (false);
    muteButton.setClickingTogglesState (false);

    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC2200));
    muteButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

    soloButton.onClick = [this] { soloClicked(); };
    muteButton.onClick = [this] { muteClicked(); };

    soloButton.setToggleState (track.isSolo  (false), juce::dontSendNotification);
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);

    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setJustificationType (juce::Justification::centred);
    trackNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    trackNameLabel.setFont (juce::FontOptions (10.0f));
    // Double-click to rename — bus/FX channels must be renameable from the
    // mixer too (live-test finding M2); same pattern as TrackHeader/ChannelStrip.
    trackNameLabel.setEditable (false, true, false);
    trackNameLabel.onTextChange = [this]
    {
        const auto newName = trackNameLabel.getText().trim();
        if (newName.isEmpty() || newName == track.getName())
        {
            trackNameLabel.setText (track.getName(), juce::dontSendNotification);
            return;
        }
        track.setName (newName);
    };

    addAndMakeVisible (volumeFader);
    addAndMakeVisible (meterDisplay);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (insertSlotPanel);
    addAndMakeVisible (trackNameLabel);

    track.state.addListener (this);
}

BusFXStrip::~BusFXStrip()
{
    track.state.removeListener (this);
}

void BusFXStrip::resized()
{
    auto area = getLocalBounds();

    const int kNameH = 18;
    const int kBtnH  = 22;
    const int kSlotH = InsertSlotPanel::kVisibleSlots * InsertSlotPanel::kSlotHeight;

    trackNameLabel.setBounds (area.removeFromBottom (kNameH));

    auto btnArea = area.removeFromBottom (kBtnH);
    soloButton.setBounds (btnArea.removeFromLeft (btnArea.getWidth() / 2).reduced (1));
    muteButton.setBounds (btnArea.reduced (1));

    insertSlotPanel.setBounds (area.removeFromBottom (juce::jmax (0, kSlotH)));

    const int kMeterW = 22;
    meterDisplay.setBounds (area.removeFromRight (juce::jmax (0, kMeterW)));
    volumeFader.setBounds  (area);
}

void BusFXStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff222228));

    // Accent stripe at top — blue for Bus, purple for FX
    const bool isFX = MixerEngine::isFXChannel (track);
    g.setColour (isFX ? juce::Colour (0xff882299) : juce::Colour (0xff0055cc));
    g.fillRect (0, 0, getWidth(), 4);

    // Right border
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ((float)getWidth() - 1.0f, 0.0f,
                (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);

    // Selection highlight border
    if (isSelected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRect (getLocalBounds(), 2);
    }
}

void BusFXStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        if (onTrackSelected)
            onTrackSelected (&track);
}

void BusFXStrip::setSelected (bool s)
{
    if (isSelected != s)
    {
        isSelected = s;
        repaint();
    }
}

void BusFXStrip::setMeterValues (float peakDB, float /*rmsDB*/, float /*lufsM*/, float /*lufsI*/)
{
    meterDisplay.setValues (peakDB);
}

void BusFXStrip::refresh()
{
    soloButton.setToggleState (track.isSolo  (false), juce::dontSendNotification);
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);

    // Sync name (renamed elsewhere / undo) — skip while the inline editor is open
    if (! trackNameLabel.isBeingEdited())
        trackNameLabel.setText (track.getName(), juce::dontSendNotification);

    repaint();
}

void BusFXStrip::soloClicked()
{
    mixerEngine.setSolo (track, ! track.isSolo (false));
    soloButton.setToggleState (track.isSolo (false), juce::dontSendNotification);
    if (onSoloClicked) onSoloClicked();
}

void BusFXStrip::muteClicked()
{
    mixerEngine.setMute (track, ! track.isMuted (false));
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    if (onMuteClicked) onMuteClicked();
}

void BusFXStrip::volumeChanged()
{
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Volume fader");
    mixerEngine.setVolume (track, (float)volumeFader.getValue());
}

//==============================================================================
// ValueTree::Listener

void BusFXStrip::valueTreePropertyChanged (juce::ValueTree& /*tree*/,
                                            const juce::Identifier& property)
{
    if (property == te::IDs::volume)
    {
        const float dB = mixerEngine.getVolume (track);
        volumeFader.setValue (dB, juce::dontSendNotification);
    }
    // Sync name label when the track is renamed elsewhere (arrangement, undo).
    // May fire off the message thread → defer via callAsync (same as `enabled`).
    else if (property == te::IDs::name)
    {
        juce::Component::SafePointer<BusFXStrip> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->refresh();
        });
    }
    // Live-sync insert-slot bypass LEDs when a plugin's enabled state changes
    // externally (e.g. toggled from its floating editor window). Property changes
    // propagate up from the PLUGIN child node to this listener on the track state.
    // May fire off the message thread (atomic CachedValue) → defer via callAsync.
    else if (property == te::IDs::enabled)
    {
        juce::Component::SafePointer<BusFXStrip> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->insertSlotPanel.repaint();
        });
    }
}

void BusFXStrip::valueTreeChildAdded (juce::ValueTree& /*parent*/, juce::ValueTree& child)
{
    if (child.hasType (juce::Identifier ("PLUGIN")))
        insertSlotPanel.refresh();
}

void BusFXStrip::valueTreeChildRemoved (juce::ValueTree& /*parent*/, juce::ValueTree& child, int)
{
    if (child.hasType (juce::Identifier ("PLUGIN")))
        insertSlotPanel.refresh();
}

void BusFXStrip::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // Refresh insert slots when the chain is reordered (drag-reorder, undo/redo).
    // Only react if the reordered node actually contains PLUGIN children.
    for (int i = 0; i < parent.getNumChildren(); ++i)
    {
        if (parent.getChild (i).hasType (juce::Identifier ("PLUGIN")))
        {
            insertSlotPanel.refresh();
            return;
        }
    }
}
