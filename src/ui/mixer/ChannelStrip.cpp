#include "ChannelStrip.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "plugins/BuiltInPluginFormat.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"

//==============================================================================
// MeterBar

MeterBar::MeterBar (MeterProcessor& meterProcessor_)
    : meterProcessor (meterProcessor_)
{
    // Primary peak dB label — main numeric readout matching Logic Pro / Studio One
    peakLabel.setText ("---", juce::dontSendNotification);
    peakLabel.setJustificationType (juce::Justification::centred);
    peakLabel.setColour (juce::Label::textColourId, juce::Colour (0xffdddddd));
    peakLabel.setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (peakLabel);

    // Secondary LUFS-I label — informational only, smaller font below peak readout
    lufsLabel.setText ("---L", juce::dontSendNotification);
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colour (0xff777777));
    lufsLabel.setFont (juce::FontOptions (9.0f));
    addAndMakeVisible (lufsLabel);

    // No timer started here — MeterBar is purely push-driven.
    // All updates arrive via setMeterValues() called from MixerView::timerCallback().
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
            ? "---L"
            : juce::String (displayLUFSI, 1) + "L";
        lufsLabel.setText (lufsText, juce::dontSendNotification);
        needsRepaint = true;
    }

    // Update primary peak dB label on any peak change
    if (needsRepaint)
    {
        const juce::String peakText = displayPeakDB <= -99.0f
            ? "---"
            : juce::String (displayPeakDB, 1);
        peakLabel.setText (peakText, juce::dontSendNotification);
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
    // Bottom 29px: LUFS-I secondary (13px) then peak dB primary (16px) above it
    auto area = getLocalBounds();
    lufsLabel.setBounds  (area.removeFromBottom (13));
    peakLabel.setBounds  (area.removeFromBottom (16));
}

void MeterBar::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    // Reserve bottom 29px for peak dB label (16px) + LUFS-I secondary label (13px)
    area.removeFromBottom (29.0f);

    // Narrow mode: when the meter is placed alongside the fader it is ~22px wide.
    // In that case skip the dB scale column and the dual-bar layout — draw a single
    // peak bar at full width with tick marks only.
    const bool narrowMode = (getWidth() < 40);

    // Background
    g.setColour (juce::Colour (0xff111111));
    g.fillRect (area);

    if (narrowMode)
    {
        // ---- Single peak bar, full width ----
        const float norm = dbToNorm (displayPeakDB);
        const float barH = norm * area.getHeight();
        const float barY = area.getBottom() - barH;

        juce::ColourGradient grad;
        grad.isRadial = false;
        grad.point1 = { area.getX(), area.getBottom() };
        grad.point2 = { area.getX(), area.getY() };
        grad.addColour (0.0, juce::Colour (0xff00aa44));
        grad.addColour (0.7, juce::Colour (0xffddcc00));
        grad.addColour (0.9, juce::Colour (0xffff4400));
        grad.addColour (1.0, juce::Colour (0xffff0000));

        g.setGradientFill (grad);
        g.fillRect (area.getX(), barY, area.getWidth(), barH);

        // Peak hold line
        if (peakHoldDB > -99.0f)
        {
            const float holdNorm = dbToNorm (peakHoldDB);
            const float holdY = area.getBottom() - holdNorm * area.getHeight();
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawHorizontalLine ((int)holdY, area.getX(), area.getRight());
        }

        // Subtle tick marks at -6 and -18 dBFS (no labels — no room)
        for (float dB : { -6.0f, -18.0f })
        {
            const float tickY = area.getBottom() - dbToNorm (dB) * area.getHeight();
            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine ((int)tickY, area.getX(), area.getRight());
        }
    }
    else
    {
        // ---- Wide mode: dB scale column on left, Peak + RMS bars on right ----
        const float kScaleW = 16.0f;
        const float barsLeft  = area.getX() + kScaleW;
        const float barsWidth = area.getRight() - barsLeft;
        const float barW      = (barsWidth - 2.0f) * 0.5f;
        const float barX      = barsLeft;

        // Peak bar
        {
            const float norm = dbToNorm (displayPeakDB);
            const float barH = norm * area.getHeight();
            const float barY = area.getBottom() - barH;

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

            if (peakHoldDB > -99.0f)
            {
                const float holdNorm = dbToNorm (peakHoldDB);
                const float holdY = area.getBottom() - holdNorm * area.getHeight();
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawHorizontalLine ((int)holdY, barX, barX + barW);
            }
        }

        // RMS bar
        {
            const float rmsNorm = dbToNorm (displayRMSDB);
            const float rmsH    = rmsNorm * area.getHeight();
            const float rmsY    = area.getBottom() - rmsH;
            const float rmsX    = barX + barW + 2.0f;
            g.setColour (juce::Colour (0xff226688));
            g.fillRect (rmsX, rmsY, barW, rmsH);
        }

        // Tick marks + dB scale labels
        for (float dB : { -6.0f, -12.0f, -18.0f, -24.0f })
        {
            const float norm  = dbToNorm (dB);
            const float tickY = area.getBottom() - norm * area.getHeight();

            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine ((int)tickY, barsLeft, area.getRight());

            g.setFont (juce::FontOptions (8.0f));
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (juce::String (static_cast<int> (dB)),
                        (int)area.getX(),
                        (int)(tickY - 7.0f),
                        (int)kScaleW - 2,
                        14,
                        juce::Justification::centredRight,
                        false);
        }
    }
}

//==============================================================================
// InsertSlotPanel::SlotButton

void ChannelStrip::InsertSlotPanel::SlotButton::paint (juce::Graphics& g)
{
    const bool occupied = (plugin != nullptr);

    // Background — dim when bypassed
    const bool bypassed = occupied && !plugin->isEnabled();
    g.fillAll (occupied ? (bypassed ? juce::Colour (0xff1e1e1e) : juce::Colour (0xff2a2a2a))
                        : juce::Colour (0xff1a1a1a));

    // Border
    if (occupied)
    {
        g.setColour (bypassed ? juce::Colour (0xff444444) : juce::Colour (0xff666666));
        g.drawRect (getLocalBounds(), 1);
    }
    else
    {
        g.setColour (juce::Colour (0xff444444));
        g.drawRect (getLocalBounds(), 1);
    }

    // 11pt — plugin names must be readable at a glance (Studio One size); 8pt was too small
    g.setFont (juce::FontOptions (11.0f));

    if (occupied)
    {
        // Bypass dot — 6×6 circle on the right edge: green=active, grey=bypassed
        const int dotSize = 6;
        const int dotX = getWidth() - dotSize - 3;
        const int dotY = (getHeight() - dotSize) / 2;
        g.setColour (bypassed ? juce::Colour (0xff555555) : juce::Colour (0xff44cc44));
        g.fillEllipse ((float)dotX, (float)dotY, (float)dotSize, (float)dotSize);

        // Plugin name in remaining area, greyed-out when bypassed
        g.setColour (bypassed ? juce::Colour (0xff666666) : juce::Colours::white);
        g.drawText (plugin->getName(),
                    getLocalBounds().withTrimmedRight (dotSize + 6).reduced (2, 1),
                    juce::Justification::centredLeft, true);
    }
    else
    {
        // "+" centred in grey
        g.setColour (juce::Colour (0xff888888));
        g.drawText ("+", getLocalBounds(), juce::Justification::centred, false);
    }
}

void ChannelStrip::InsertSlotPanel::SlotButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return;
    }

    // Click on bypass dot area (rightmost 12px) toggles bypass
    if (plugin != nullptr && e.x >= getWidth() - 12)
    {
        plugin->setEnabled (!plugin->isEnabled());
        repaint();
        return;
    }

    if (onClick)
        onClick();
}

void ChannelStrip::InsertSlotPanel::SlotButton::mouseDrag (const juce::MouseEvent& e)
{
    if (plugin == nullptr)
        return;   // empty "+" slot cannot be dragged

    if (! dragging && e.getDistanceFromDragStart() < 5)
        return;   // click/bypass gesture, not a drag yet

    dragging = true;
    if (onDragMove)
        onDragMove (e);
}

void ChannelStrip::InsertSlotPanel::SlotButton::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        if (onDragEnd)
            onDragEnd (e);
    }
}

void ChannelStrip::InsertSlotPanel::SlotButton::mouseDoubleClick (const juce::MouseEvent& /*e*/)
{
    if (onDoubleClick)
        onDoubleClick();
}

//==============================================================================
// InsertSlotPanel

ChannelStrip::InsertSlotPanel::InsertSlotPanel (te::Track& track_,
                                                 MixerEngine& mixerEngine_,
                                                 PluginManager& pluginManager_,
                                                 PluginWindowManager& pluginWindowManager_,
                                                 PluginScanner& pluginScanner_)
    : track (track_)
    , mixerEngine (mixerEngine_)
    , pluginManager (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
{
    // Scrollable viewport — no hard slot limit.
    // refresh() creates exactly (pluginCount + 1) slots; all are in slotsContent
    // which can grow beyond the visible 6-slot window.
    slotsViewport.setViewedComponent (&slotsContent, false);
    slotsViewport.setScrollBarsShown (true, false);   // vertical scroll only
    addAndMakeVisible (slotsViewport);

    refresh();
}

void ChannelStrip::InsertSlotPanel::resized()
{
    slotsViewport.setBounds (getLocalBounds());

    // Keep content width in sync when the strip is resized
    const int contentW = juce::jmax (1, getWidth());
    const int contentH = juce::jmax (kSlotHeight, slots.size() * kSlotHeight);
    slotsContent.setSize (contentW, contentH);
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setBounds (0, i * kSlotHeight, contentW, kSlotHeight);
}

void ChannelStrip::InsertSlotPanel::paint (juce::Graphics& g)
{
    // Subtle background for the insert panel area
    g.fillAll (juce::Colour (0xff181818));
}

void ChannelStrip::InsertSlotPanel::paintOverChildren (juce::Graphics& g)
{
    // Insertion indicator while drag-reordering: accent line at the target slot's top edge
    if (dragTargetSlot < 0)
        return;

    const auto topInContent = juce::Point<int> (0, dragTargetSlot * kSlotHeight);
    const auto topInPanel   = getLocalPoint (&slotsContent, topInContent);

    g.setColour (juce::Colour (0xff44aaff));
    g.fillRect (0, topInPanel.y - 1, getWidth(), 2);
}

int ChannelStrip::InsertSlotPanel::slotIndexForEvent (const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (&slotsContent).getPosition();
    return juce::jlimit (0, juce::jmax (0, slots.size() - 1), pos.y / kSlotHeight);
}

void ChannelStrip::InsertSlotPanel::commitSlotDrag (int fromSlot, int toSlot)
{
    if (fromSlot != toSlot)
        pluginManager.moveUserPluginToSlot (track, fromSlot, toSlot);

    refresh();
}

void ChannelStrip::InsertSlotPanel::refresh()
{
    // Destroy existing slots — Component destructor auto-removes from slotsContent.
    slots.clear();

    // Collect user-visible plugins (skip internal TE volume/level plugins).
    const auto chain = pluginManager.getPluginChain (track);
    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : chain)
    {
        const juce::String t = p->getPluginType();
        if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
            userPlugins.add (p);
    }

    // No hard limit — create (pluginCount + 1) slots: last is always the empty "+" add slot.
    const int numSlots = juce::jmax (1, userPlugins.size() + 1);

    const int contentW = juce::jmax (1, getWidth());
    for (int i = 0; i < numSlots; ++i)
    {
        auto* slot = slots.add (new SlotButton());

        if (i < userPlugins.size())
            slot->plugin = userPlugins[i];  // occupied
        // else plugin == nullptr → empty "+" slot

        // Der Slot-Text wird GEZEICHNET — ohne diesen Namen ist ein Insert-Platz
        // weder fuer VoiceOver noch fuer eine Adressierung ueber Namen vorhanden,
        // und ob er belegt ist, waere nur am Bild ablesbar.
        slot->setTitle ("Insert " + juce::String (i + 1) + " " + track.getName()
                        + (slot->plugin != nullptr
                             ? ": " + slot->plugin->getName()
                             : juce::String (" (leer)")));

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
                showSlotContextMenu (slots[slotIndex]->plugin);
        };

        // Drag-reorder: track the hovered slot while dragging, commit on release.
        slot->onDragMove = [this, slotIndex] (const juce::MouseEvent& e)
        {
            juce::ignoreUnused (slotIndex);
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

    // Update scrollable content height to fit all slots
    slotsContent.setSize (contentW, numSlots * kSlotHeight);

    repaint();
}

void ChannelStrip::InsertSlotPanel::showAddPluginMenu (int slotIndex)
{
    // Map the user-visible slot index to the raw chain index. Internal TE
    // plugins (volume/level/auxreturn/auxsend) can sit anywhere in the raw
    // chain — a simple prefix offset placed new plugins between existing user
    // plugins (live-test finding M3). PluginManager owns the correct mapping.
    const int actualIndex = pluginManager.chainInsertIndexForUserSlot (track, slotIndex);

    juce::PopupMenu menu;

    // Built-in FX section
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
                // Open full plugin browser popup
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
                // Built-in FX: result is 1-based index into descs
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

void ChannelStrip::InsertSlotPanel::showSlotContextMenu (te::Plugin* plugin)
{
    if (plugin == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (1, "Remove \"" + plugin->getName() + "\"");

    // Sidechain source submenu — only for plugins that expose a key input
    // (te::Plugin::canSidechain() == true once the processor reports >2 ins).
    juce::Array<te::AudioTrack*> sourceTracks;
    if (plugin->canSidechain())
    {
        juce::PopupMenu scMenu;
        const auto currentSource = plugin->getSidechainSourceID();

        scMenu.addItem (2, "(Keine)", true, ! currentSource.isValid());
        scMenu.addSeparator();

        int itemId = 3;
        for (auto* t : te::getAudioTracks (track.edit))
        {
            if (t == &track)
                continue;   // a track can't key itself

            sourceTracks.add (t);
            scMenu.addItem (itemId++, t->getName(), true, currentSource == t->itemID);
        }

        menu.addSeparator();
        menu.addSubMenu ("Sidechain-Quelle", scMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, plugin, sourceTracks] (int result)
        {
            if (result == 1)
            {
                pluginManager.removePlugin (track, *plugin);
                refresh();
            }
            else if (result == 2)
            {
                mixerEngine.clearSidechainSource (*plugin);
            }
            else if (result >= 3)
            {
                const int idx = result - 3;
                if (idx < sourceTracks.size())
                    mixerEngine.setSidechainSource (*plugin, *sourceTracks[idx]);
            }
        });
}

//==============================================================================
// ChannelStrip

ChannelStrip::ChannelStrip (te::AudioTrack& track_, MixerEngine& mixerEngine_,
                             MeterProcessor& meterProcessor_,
                             PluginManager& pluginManager_,
                             PluginWindowManager& pluginWindowManager_,
                             PluginScanner& pluginScanner_)
    : track (track_)
    , mixerEngine (mixerEngine_)
    , pluginManager (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
    , meterBar (meterProcessor_)
    , insertSlotPanel (track_, mixerEngine_, pluginManager_, pluginWindowManager_, pluginScanner_)
    , sendSlotPanel (track_, mixerEngine_)
{
    // ---- Volume fader ----
    volumeFader.setSliderStyle    (juce::Slider::LinearVertical);
    volumeFader.setTextBoxStyle   (juce::Slider::TextBoxBelow, false, 50, 16);
    volumeFader.setRange          (-60.0, 6.0, 0.1);
    volumeFader.setDoubleClickReturnValue (true, 0.0);  // double-click resets to 0 dB
    volumeFader.setTextValueSuffix(" dB");
    volumeFader.setValue          (mixerEngine.getVolume (track), juce::dontSendNotification);
    volumeFader.onValueChange = [this]
    {
        volumeChanged();                                   // always moves this strip's own track
        if (onVolumeChanged) onVolumeChanged (volumeFader.getValue());  // group sync (MixerView)
    };
    volumeFader.onDragStart = [this]
    {
        track.edit.getUndoManager().beginNewTransaction ("Volume fader");
        volumeGestureActive = true;
        if (onVolumeDragStart) onVolumeDragStart();
    };
    volumeFader.onDragEnd = [this]
    {
        if (onVolumeDragEnd) onVolumeDragEnd();
        volumeGestureActive = false;
        track.edit.getUndoManager().beginNewTransaction();
    };

    // ---- Pan knob ----
    panKnob.setSliderStyle     (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setTextBoxStyle    (juce::Slider::NoTextBox, false, 0, 0);
    panKnob.setRange           (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);  // double-click resets centre
    panKnob.setValue           (mixerEngine.getPan (track), juce::dontSendNotification);
    panKnob.setLookAndFeel     (&panLnf);           // UX4: S1-Arc statt Default-Rotary
    panKnob.setMouseDragSensitivity (380);          // UX4: feinere Kontrolle (Default 250)
    panKnob.onValueChange = [this] { panChanged(); };
    // panValueLabel initial text is set after panValueLabel is constructed (below)

    // ---- Solo / Mute buttons ----
    // Auto-toggle disabled: we set the visual state ourselves from engine truth
    // in the onClick handlers so it never diverges from what TE actually applied.
    soloButton.setClickingTogglesState (false);
    muteButton.setClickingTogglesState (false);

    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC2200));
    muteButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

    soloButton.onClick = [this]
    {
        mixerEngine.setSolo (track, ! track.isSolo (false));
        soloButton.setToggleState (track.isSolo (false), juce::dontSendNotification);
        if (onSoloClicked) onSoloClicked();
    };
    muteButton.onClick = [this]
    {
        mixerEngine.setMute (track, ! track.isMuted (false));
        muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
        if (onMuteClicked) onMuteClicked();
    };

    // ---- Pan value label (numeric readout: L33 / C / R48) ----
    // UX4: Doppelklick = Wert eintippen ("L45", "R30", "C", "-20"); ungültige
    // Eingabe stellt den alten Wert wieder her.
    panValueLabel.setText (aizana::panvalue::format (panKnob.getValue()), juce::dontSendNotification);
    panValueLabel.setJustificationType (juce::Justification::centred);
    panValueLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    panValueLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    panValueLabel.setEditable (false, true, false);
    panValueLabel.setTooltip (juce::String::fromUTF8 ("Doppelklick: Wert eingeben (L45, R30, C)"));
    panValueLabel.onTextChange = [this]
    {
        double pan = 0.0;
        if (aizana::panvalue::parse (panValueLabel.getText(), pan))
            panKnob.setValue (pan, juce::sendNotification);   // → panChanged (1 Undo)
        else
            panValueLabel.setText (aizana::panvalue::format (panKnob.getValue()),
                                   juce::dontSendNotification);
    };

    // ---- Pan label ----
    panLabel.setText ("PAN", juce::dontSendNotification);
    panLabel.setJustificationType (juce::Justification::centred);
    panLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    panLabel.setFont (juce::FontOptions (9.0f));

    // ---- Output routing button ----
    // Shows "▶ Master" when going directly to master, or "▶ <BusName>" when routed to a bus.
    // Clicking opens a popup to re-route to any available Bus Channel or back to Master.
    outputButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Master"));
    outputButton.setClickingTogglesState (false);
    outputButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff222222));
    outputButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff336688));
    outputButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff999999));
    outputButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    outputButton.onClick = [this] { showOutputRoutingMenu(); };

    // ---- Track name label ----
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setJustificationType (juce::Justification::centred);
    trackNameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    trackNameLabel.setFont (juce::FontOptions (12.0f));
    // Double-click to rename — same pattern as the arrangement's TrackHeader,
    // so tracks are renameable from the mixer too (live-test finding M2).
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
    // Route trackNameLabel clicks to ChannelStrip::mouseDown() so clicking the name
    // selects this strip even though child components cover all other strip surfaces.
    trackNameLabel.addMouseListener (this, false);

    // Set initial pan value label from current knob state
    panValueLabel.setText (formatPanValue (panKnob.getValue()), juce::dontSendNotification);

    addAndMakeVisible (meterBar);
    addAndMakeVisible (volumeFader);
    addAndMakeVisible (panKnob);
    addAndMakeVisible (panValueLabel);
    addAndMakeVisible (panLabel);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (outputButton);
    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (insertSlotPanel);
    addAndMakeVisible (sendSlotPanel);
    sendSlotPanel.setVisible (true);  // visible on all strips — send rows appear when sends exist

    // Listen to track's ValueTree so we can sync fader when state changes externally
    // (e.g., undo/redo, AI automation writes) and refresh insert slots on plugin changes
    track.state.addListener (this);

    // Initialise output button to reflect actual routing state.
    // rebuildStrips() creates new ChannelStrips after routing changes; the constructor
    // defaults to "▶ Master" which is wrong if the track is already routed to a bus.
    refreshOutputButton();

    // Namen fuer Bedienungshilfen/Automatisierung sofort setzen — refresh()
    // laeuft beim Aufbau nicht zwingend.
    updateAccessibleNames();
}

ChannelStrip::~ChannelStrip()
{
    panKnob.setLookAndFeel (nullptr);   // UX4: LnF-Member stirbt mit diesem Objekt
    track.state.removeListener (this);
}

void ChannelStrip::resized()
{
    // Professional DAW channel strip layout:
    //
    //  [top]  ─────────────────────────────────
    //         Pan knob (36px) + value/label rows
    //  ─────────────────────────────────────────
    //         Fader  │  Meter  ← side by side, tall
    //         (left) │ (22px right)
    //  ─────────────────────────────────────────
    //         [S] [M]  solo/mute (22px)
    //  ─────────────────────────────────────────
    //         Insert slots — scrollable, 6 visible
    //  ─────────────────────────────────────────
    //  [bot]  Track name (18px)
    //
    // The meter runs alongside the fader (tall, vertical), matching Logic Pro / Studio One.
    // Meter labels (peak dB + LUFS) sit at the bottom of the meter area.

    auto area = getLocalBounds();

    // ---- Fixed-height bottom elements (allocated from bottom up) ----
    const int kNameH   = 24;
    const int kOutputH = 16;  // output routing button row
    const int kSlotH   = InsertSlotPanel::kVisibleSlots * InsertSlotPanel::kSlotHeight; // 96
    const int kSendH   = sendSlotPanel.getPreferredHeight();   // sends + "+ Send" row
    const int kBtnH    = 22;

    auto nameArea    = area.removeFromBottom (kNameH);
    auto outputArea  = area.removeFromBottom (kOutputH);
    auto slotArea    = area.removeFromBottom (juce::jmax (0, kSlotH));
    auto sendArea    = area.removeFromBottom (juce::jmax (0, kSendH));
    auto btnArea     = area.removeFromBottom (juce::jmax (0, kBtnH));

    // ---- Pan section — allocated from the top ----
    const int kPanH    = 48;   // UX4: größerer Knob (war 36 — zu fummelig)
    const int kPanValH = 15;
    const int kPanLblH = 11;

    auto panArea    = area.removeFromTop (juce::jmax (0, kPanH));
    auto panValArea = area.removeFromTop (juce::jmax (0, kPanValH));
    auto panLblArea = area.removeFromTop (juce::jmax (0, kPanLblH));

    // ---- Remaining area: fader (left) + meter (right 28px) side by side ----
    const int kMeterW = 28;
    auto meterArea = area.removeFromRight (juce::jmax (0, kMeterW));
    auto faderArea = area;  // everything remaining

    meterBar.setBounds      (meterArea);
    volumeFader.setBounds   (faderArea);
    panKnob.setBounds       (panArea);
    panValueLabel.setBounds (panValArea);
    panLabel.setBounds      (panLblArea);

    // Solo and Mute buttons side by side
    soloButton.setBounds (btnArea.removeFromLeft (btnArea.getWidth() / 2).reduced (1));
    muteButton.setBounds (btnArea.reduced (1));

    insertSlotPanel.setBounds  (slotArea);
    sendSlotPanel.setBounds    (sendArea);
    outputButton.setBounds     (outputArea.reduced (1, 1));
    trackNameLabel.setBounds   (nameArea);
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

    // Selection highlight border
    if (isSelected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRect (getLocalBounds(), 2);
    }
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        if (onTrackSelected)
            onTrackSelected (&track);
}

void ChannelStrip::setSelected (bool s)
{
    if (isSelected != s)
    {
        isSelected = s;
        repaint();
    }
}

void ChannelStrip::setMeterValues (float peakDB, float rmsDB, float lufsM, float lufsI)
{
    // Delegate to the MeterBar — all display logic lives there.
    meterBar.setMeterValues (peakDB, rmsDB, lufsM, lufsI);
}

void ChannelStrip::rebuildSends (const juce::Array<te::AudioTrack*>& fxChannels)
{
    sendSlotPanel.rebuild (fxChannels);
}

void ChannelStrip::volumeChanged()
{
    if (volumeGestureActive)
        mixerEngine.setVolumeInCurrentTransaction (track, (float) volumeFader.getValue());
    else
        mixerEngine.setVolume (track, (float) volumeFader.getValue());
}

void ChannelStrip::followAutomation()
{
    // Anzeige-Sync aus dem Parameter-currentValue (Automations-Playback schreibt
    // nie ins ValueTree, deshalb greift der ValueTree-Listener hier nicht).
    // NIEMALS zurückschreiben — nur setValue mit dontSendNotification.
    if (! volumeFader.isMouseButtonDown())
    {
        const float dB = mixerEngine.getLiveVolumeDb (track);
        if (std::abs ((float) volumeFader.getValue() - dB) > 0.05f)
            volumeFader.setValue (dB, juce::dontSendNotification);
    }

    if (! panKnob.isMouseButtonDown())
    {
        const float pan = mixerEngine.getLivePan (track);
        if (std::abs ((float) panKnob.getValue() - pan) > 0.005f)
        {
            panKnob.setValue (pan, juce::dontSendNotification);
            panValueLabel.setText (formatPanValue (pan), juce::dontSendNotification);
        }
    }
}

void ChannelStrip::updateAccessibleNames()
{
    // Mit dem KANALNAMEN qualifizieren: "S"/"M" allein ist auf zwanzig Zuegen
    // zwanzigmal dasselbe — fuer VoiceOver nichtssagend und fuer eine
    // Automatisierung mehrdeutig (ein Klick trifft irgendeinen davon, ohne dass
    // es auffaellt). Sichtbar aendert sich nichts, setTitle zeichnet nicht.
    const auto n = track.getName();
    setTitle (juce::String::fromUTF8 ("Kanalzug ") + n);
    volumeFader  .setTitle (juce::String::fromUTF8 ("Lautstaerke ") + n);
    panKnob      .setTitle (juce::String::fromUTF8 ("Panorama ") + n);
    soloButton   .setTitle ("Solo " + n);
    muteButton   .setTitle (juce::String::fromUTF8 ("Stumm ") + n);
    outputButton .setTitle (juce::String::fromUTF8 ("Ausgang ") + n);
    trackNameLabel.setTitle (juce::String::fromUTF8 ("Kanalname ") + n);
    insertSlotPanel.setTitle (juce::String::fromUTF8 ("Insert-Plaetze ") + n);
    sendSlotPanel .setTitle (juce::String::fromUTF8 ("Sends ") + n);
}

void ChannelStrip::refresh()
{
    updateAccessibleNames();

    // Pull current engine state and update all controls without firing callbacks.
    // Called from valueTreePropertyChanged (via callAsync) and from MixerView
    // keyPressed() after S/M keyboard shortcuts so visuals never diverge from TE.
    const float dB = mixerEngine.getVolume (track);
    volumeFader.setValue (dB, juce::dontSendNotification);

    const float pan = mixerEngine.getPan (track);
    panKnob.setValue (pan, juce::dontSendNotification);
    panValueLabel.setText (formatPanValue (pan), juce::dontSendNotification);

    soloButton.setToggleState (track.isSolo  (false), juce::dontSendNotification);
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);

    // Sync name (renamed in arrangement / undo) — skip while the inline editor is open
    if (! trackNameLabel.isBeingEdited())
        trackNameLabel.setText (track.getName(), juce::dontSendNotification);

    refreshOutputButton();
    repaint();
}

juce::String ChannelStrip::formatPanValue (double pan)
{
    return aizana::panvalue::format (pan);   // geteilter, golden-getesteter Helper (UX4)
}

void ChannelStrip::panChanged()
{
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Pan knob");
    mixerEngine.setPan (track, (float)panKnob.getValue());
    panValueLabel.setText (formatPanValue (panKnob.getValue()), juce::dontSendNotification);
}

void ChannelStrip::refreshOutputButton()
{
    // Primary: TE routing API
    auto* bus = mixerEngine.getBusChannelFor (track);
    if (bus != nullptr)
    {
        outputButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 ") + bus->getName());
        outputButton.setToggleState (true, juce::dontSendNotification);
        return;
    }

    // Fallback: kOutputBusNameProp cached by routeTrackToBus().
    // getDestinationTrack() can transiently return nullptr during TE graph rebuilds
    // even when routing is still active — this property is our stable display source.
    const juce::String cachedBus =
        track.state.getProperty (kOutputBusNameProp, juce::String()).toString();
    if (cachedBus.isNotEmpty())
    {
        outputButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 ") + cachedBus);
        outputButton.setToggleState (true, juce::dontSendNotification);
        return;
    }

    // Going to master
    outputButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6 Master"));
    outputButton.setToggleState (false, juce::dontSendNotification);
}

void ChannelStrip::showOutputRoutingMenu()
{
    const auto busChannels = mixerEngine.getBusChannels();
    auto* currentBus = mixerEngine.getBusChannelFor (track);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Output");

    // "Master" option — always first
    menu.addItem (1, "Master", true, currentBus == nullptr);

    if (! busChannels.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Bus Channels");
        for (int i = 0; i < busChannels.size(); ++i)
        {
            auto* bus = busChannels[i];
            const bool isCurrent = (bus == currentBus);
            menu.addItem (i + 2, bus->getName(), true, isCurrent);
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outputButton),
        [this, busChannels] (int result)
        {
            if (result <= 0)
                return;

            te::AudioTrack* selectedBus = nullptr;

            if (result == 1)
            {
                // Route to master
                mixerEngine.unrouteFromBus (track);
                selectedBus = nullptr;
            }
            else
            {
                const int busIndex = result - 2;
                if (busIndex >= 0 && busIndex < busChannels.size())
                {
                    selectedBus = busChannels[busIndex];
                    mixerEngine.routeTrackToBus (track, *selectedBus);
                }
            }

            refreshOutputButton();

            // Notify MixerView to apply the same routing to all other selected strips.
            if (onOutputRouted)
                onOutputRouted (selectedBus);
        });
}

void ChannelStrip::valueTreePropertyChanged (juce::ValueTree& /*tree*/,
                                              const juce::Identifier& property)
{
    // ValueTree callbacks may arrive on any thread (including the audio thread).
    // UI mutations must be deferred to the message thread via callAsync.
    // refresh() re-reads all engine state atomically (volume, pan, solo, mute).
    // Refresh on volume/pan/solo/mute changes AND on any other property that may
    // represent output routing state (TE stores track output target in the track's
    // child ValueTree nodes, so we refresh for all property changes).
    juce::ignoreUnused (property);
    {
        // SafePointer prevents the dangling-this crash: if rebuildStrips()
        // destroys this ChannelStrip before callAsync fires (e.g. on file drop),
        // the pointer becomes null and the lambda exits safely.
        juce::Component::SafePointer<ChannelStrip> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->refresh();
        });
    }
}

void ChannelStrip::valueTreeChildAdded (juce::ValueTree& /*parent*/, juce::ValueTree& child)
{
    // Refresh insert slots when a plugin is added to the track
    if (child.hasType (juce::Identifier ("PLUGIN")))
        insertSlotPanel.refresh();
}

void ChannelStrip::valueTreeChildRemoved (juce::ValueTree& /*parent*/, juce::ValueTree& child, int)
{
    // Refresh insert slots when a plugin is removed from the track
    if (child.hasType (juce::Identifier ("PLUGIN")))
        insertSlotPanel.refresh();
}

void ChannelStrip::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // Refresh insert slots when the chain is reordered (drag-reorder, undo/redo).
    // Only react if the reordered node actually contains PLUGIN children — the
    // listener sees the whole track subtree (clips etc. reorder too).
    for (int i = 0; i < parent.getNumChildren(); ++i)
    {
        if (parent.getChild (i).hasType (juce::Identifier ("PLUGIN")))
        {
            insertSlotPanel.refresh();
            return;
        }
    }
}
