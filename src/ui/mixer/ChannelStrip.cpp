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
    // LUFS-I label — below the meter bar
    // REQUIREMENTS.md + CONTEXT.md: informational only, no target enforcement
    lufsLabel.setText ("LUFS-I", juce::dontSendNotification);
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    lufsLabel.setFont (juce::FontOptions (8.0f));
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

        // Colour gradient: green -> yellow -> red
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
// InsertSlotPanel::SlotButton

void ChannelStrip::InsertSlotPanel::SlotButton::paint (juce::Graphics& g)
{
    const bool occupied = (plugin != nullptr);

    // Background
    g.fillAll (occupied ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff1a1a1a));

    // Border
    if (occupied)
    {
        g.setColour (juce::Colour (0xff666666));
        g.drawRect (getLocalBounds(), 1);
    }
    else
    {
        // Dashed border for empty slot
        g.setColour (juce::Colour (0xff444444));
        g.drawRect (getLocalBounds().reduced (0), 1);
    }

    g.setFont (juce::FontOptions (8.0f));

    if (occupied)
    {
        // Plugin name truncated to fit, white text
        g.setColour (juce::Colours::white);
        g.drawText (plugin->getName(), getLocalBounds().reduced (2, 1),
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
    }
    else
    {
        if (onClick)
            onClick();
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
                                                 PluginManager& pluginManager_,
                                                 PluginWindowManager& pluginWindowManager_,
                                                 PluginScanner& pluginScanner_)
    : track (track_)
    , pluginManager (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
{
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto* slot = slots.add (new SlotButton());

        const int slotIndex = i;

        slot->onClick = [this, slotIndex]
        {
            auto* btn = slots[slotIndex];
            if (btn->plugin == nullptr)
                showAddPluginMenu (slotIndex);
        };

        slot->onDoubleClick = [this, slotIndex]
        {
            auto* btn = slots[slotIndex];
            if (btn->plugin != nullptr)
                pluginWindowManager.openPluginWindow (*btn->plugin);
        };

        slot->onRightClick = [this, slotIndex]
        {
            auto* btn = slots[slotIndex];
            if (btn->plugin != nullptr)
                showRemovePluginMenu (btn->plugin);
        };

        addAndMakeVisible (slot);
    }

    refresh();
}

void ChannelStrip::InsertSlotPanel::resized()
{
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setBounds (0, i * kSlotHeight, getWidth(), kSlotHeight);
}

void ChannelStrip::InsertSlotPanel::paint (juce::Graphics& g)
{
    // Subtle background for the insert panel area
    g.fillAll (juce::Colour (0xff181818));
}

void ChannelStrip::InsertSlotPanel::refresh()
{
    // Clear all slot plugin pointers
    for (auto* slot : slots)
        slot->plugin = nullptr;

    // Get the plugin chain and assign user-visible plugins to slots.
    // Skip internal TE plugins: VolumeAndPanPlugin (type="volume") and
    // LevelMeterPlugin (type="level") — these are internal housekeeping plugins
    // not meaningful for the user insert panel.
    const auto chain = pluginManager.getPluginChain (track);

    int slotIdx = 0;
    for (auto* plugin : chain)
    {
        if (slotIdx >= kMaxSlots)
            break;

        // Skip internal TE plugins by checking their xmlTypeName
        const juce::String pluginType = plugin->getPluginType();
        if (pluginType == "volume" || pluginType == "level")
            continue;

        slots[slotIdx]->plugin = plugin;
        ++slotIdx;
    }

    repaint();
}

void ChannelStrip::InsertSlotPanel::showAddPluginMenu (int slotIndex)
{
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
        [this, slotIndex, descs, browseId] (int result)
        {
            if (result <= 0)
                return;

            if (result == browseId)
            {
                // Open full plugin browser popup
                PluginBrowserPopup::showForComponent (
                    *this,
                    pluginScanner.getKnownPlugins(),
                    [this, slotIndex] (const juce::PluginDescription& desc)
                    {
                        auto* newPlugin = pluginManager.insertPlugin (track, desc, slotIndex);
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
                    auto* newPlugin = pluginManager.insertPlugin (track, descs[descIdx], slotIndex);
                    if (newPlugin != nullptr)
                    {
                        pluginWindowManager.openPluginWindow (*newPlugin);
                        refresh();
                    }
                }
            }
        });
}

void ChannelStrip::InsertSlotPanel::showRemovePluginMenu (te::Plugin* plugin)
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
    , insertSlotPanel (track_, pluginManager_, pluginWindowManager_, pluginScanner_)
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
    // panValueLabel initial text is set after panValueLabel is constructed (below)

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

    // ---- Pan value label (numeric readout: L33 / C / R48) ----
    panValueLabel.setText ("C", juce::dontSendNotification);
    panValueLabel.setJustificationType (juce::Justification::centred);
    panValueLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    panValueLabel.setFont (juce::FontOptions (9.0f));

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

    // Set initial pan value label from current knob state
    panValueLabel.setText (formatPanValue (panKnob.getValue()), juce::dontSendNotification);

    addAndMakeVisible (meterBar);
    addAndMakeVisible (volumeFader);
    addAndMakeVisible (panKnob);
    addAndMakeVisible (panValueLabel);
    addAndMakeVisible (panLabel);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (insertSlotPanel);

    // Listen to track's ValueTree so we can sync fader when state changes externally
    // (e.g., undo/redo, AI automation writes) and refresh insert slots on plugin changes
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

    // Insert slot panel above track name (6 slots * 16px = 96px)
    const int insertH = InsertSlotPanel::kMaxSlots * InsertSlotPanel::kSlotHeight;
    insertSlotPanel.setBounds (area.removeFromBottom (insertH));
    area.removeFromBottom (2);

    // Solo + Mute buttons above insert panel
    auto buttonArea = area.removeFromBottom (22);
    soloButton.setBounds (buttonArea.removeFromLeft (buttonArea.getWidth() / 2).reduced (1));
    muteButton.setBounds (buttonArea.reduced (1));

    // Without text box, the full height is the rotary dial itself — use 55px to make it prominent
    const int panH = juce::jmax (48, juce::jmin (60, area.getHeight() / 4));
    panKnob.setBounds (area.removeFromTop (panH));
    panValueLabel.setBounds (area.removeFromTop (12));  // numeric readout: L33 / C / R48
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

juce::String ChannelStrip::formatPanValue (double pan)
{
    int pct = juce::roundToInt (std::abs (pan) * 100.0);
    if (pct == 0) return "C";
    return (pan < 0.0 ? "L" : "R") + juce::String (pct);
}

void ChannelStrip::panChanged()
{
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Pan knob");
    mixerEngine.setPan (track, (float)panKnob.getValue());
    panValueLabel.setText (formatPanValue (panKnob.getValue()), juce::dontSendNotification);
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

    if (property == te::IDs::pan)
    {
        const float pan = mixerEngine.getPan (track);
        panKnob.setValue (pan, juce::dontSendNotification);
        panValueLabel.setText (formatPanValue (pan), juce::dontSendNotification);
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
