#include "MasterStrip.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "plugins/BuiltInPluginFormat.h"
#include "plugins/PluginScanner.h"

//==============================================================================
// MasterStrip::MeterDisplay

MasterStrip::MeterDisplay::MeterDisplay()
{
    peakLabel.setFont (juce::FontOptions (8.0f));
    peakLabel.setJustificationType (juce::Justification::centred);
    peakLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcccccc));
    peakLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    lufsLabel.setFont (juce::FontOptions (8.0f));
    lufsLabel.setJustificationType (juce::Justification::centred);
    lufsLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    lufsLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    addAndMakeVisible (peakLabel);
    addAndMakeVisible (lufsLabel);
}

float MasterStrip::MeterDisplay::dbToNorm (float db)
{
    if (db <= -60.0f) return 0.0f;
    if (db >= 0.0f)   return 1.0f;
    return (db + 60.0f) / 60.0f;
}

void MasterStrip::MeterDisplay::setValues (float peak, float lufsI)
{
    // Peak hold
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

    peakLabel.setText (peak > -99.0f ? juce::String (peak, 1) + " dB" : "-inf",
                       juce::dontSendNotification);
    lufsLabel.setText (lufsI > -99.0f ? juce::String (lufsI, 1) + " L" : "--",
                       juce::dontSendNotification);

    repaint();
}

void MasterStrip::MeterDisplay::resized()
{
    auto area = getLocalBounds();
    lufsLabel.setBounds  (area.removeFromBottom (13));
    peakLabel.setBounds  (area.removeFromBottom (14));
}

void MasterStrip::MeterDisplay::paint (juce::Graphics& g)
{
    const auto area    = getLocalBounds().toFloat();
    const float labelH = 27.0f;
    const auto barArea = area.withTrimmedBottom (labelH);

    // Dark background
    g.setColour (juce::Colour (0xff111111));
    g.fillRect  (barArea);

    const float norm    = dbToNorm (peakDB);
    const float barH    = barArea.getHeight() * norm;
    const auto  barRect = barArea.withTop (barArea.getBottom() - barH);

    // Gradient: green → yellow → red
    if (barH > 0.0f)
    {
        juce::ColourGradient grad (juce::Colour (0xff00cc44), barRect.getBottomLeft(),
                                   juce::Colour (0xffdd3300), barRect.getTopLeft(), false);
        grad.addColour (0.7, juce::Colour (0xffddcc00));
        g.setGradientFill (grad);
        g.fillRect (barRect);
    }

    // Peak hold line
    if (holdDB > -99.0f)
    {
        const float holdY = barArea.getBottom() - barArea.getHeight() * dbToNorm (holdDB);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawHorizontalLine (juce::roundToInt (holdY), barArea.getX(), barArea.getRight());
    }

    // Border
    g.setColour (juce::Colour (0xff333333));
    g.drawRect (barArea, 1);
}

//==============================================================================
// MasterInsertSlotPanel — SlotButton

void MasterStrip::MasterInsertSlotPanel::SlotButton::paint (juce::Graphics& g)
{
    const bool occupied = (plugin != nullptr);
    g.fillAll (juce::Colour (occupied ? 0xff2a3a2a : 0xff1a1a1a));

    g.setColour (juce::Colour (0xff333333));
    g.drawRect (getLocalBounds(), 1);

    g.setFont (juce::FontOptions (9.0f));
    g.setColour (occupied ? juce::Colour (0xff88ee88) : juce::Colour (0xff555555));
    g.drawText (occupied ? plugin->getName() : "+",
                getLocalBounds().reduced (2), juce::Justification::centredLeft, true);

    // Bypass LED for occupied slots
    if (occupied)
    {
        const bool active = plugin->isEnabled();
        g.setColour (active ? juce::Colour (0xff33dd33) : juce::Colour (0xffdd3333));
        const int ledSize = 6;
        g.fillEllipse ((float)(getWidth() - ledSize - 3),
                       (float)(getHeight() / 2 - ledSize / 2),
                       (float)ledSize, (float)ledSize);
    }
}

void MasterStrip::MasterInsertSlotPanel::SlotButton::mouseDown (const juce::MouseEvent& e)
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

void MasterStrip::MasterInsertSlotPanel::SlotButton::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

//==============================================================================
// MasterInsertSlotPanel

MasterStrip::MasterInsertSlotPanel::MasterInsertSlotPanel (te::Edit& edit_,
                                                             PluginWindowManager& pwm,
                                                             PluginScanner& ps)
    : edit (edit_)
    , pluginWindowManager (pwm)
    , pluginScanner (ps)
{
    slotsViewport.setViewedComponent (&slotsContent, false);
    slotsViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (slotsViewport);

    refresh();
}

void MasterStrip::MasterInsertSlotPanel::resized()
{
    slotsViewport.setBounds (getLocalBounds());

    const int contentW = juce::jmax (1, getWidth());
    const int contentH = juce::jmax (kSlotHeight, slots.size() * kSlotHeight);
    slotsContent.setSize (contentW, contentH);
    for (int i = 0; i < slots.size(); ++i)
        slots[i]->setBounds (0, i * kSlotHeight, contentW, kSlotHeight);
}

void MasterStrip::MasterInsertSlotPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
}

void MasterStrip::MasterInsertSlotPanel::refresh()
{
    slots.clear();

    auto& masterList = edit.getMasterPluginList();
    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : masterList)
    {
        const juce::String t = p->getPluginType();
        if (t != "volume" && t != "level")
            userPlugins.add (p);
    }

    const int numSlots  = juce::jmax (1, userPlugins.size() + 1);
    const int contentW  = juce::jmax (1, getWidth());

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

        slot->setBounds (0, i * kSlotHeight, contentW, kSlotHeight);
        slotsContent.addAndMakeVisible (slot);
    }

    slotsContent.setSize (contentW, numSlots * kSlotHeight);
    repaint();
}

void MasterStrip::MasterInsertSlotPanel::showAddPluginMenu (int slotIndex)
{
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
        [this, slotIndex, descs, browseId] (int result)
        {
            if (result <= 0)
                return;

            if (result == browseId)
            {
                PluginBrowserPopup::showForComponent (
                    *this,
                    pluginScanner.getKnownPlugins(),
                    [this, slotIndex] (const juce::PluginDescription& desc)
                    {
                        auto pluginPtr = edit.getPluginCache().createNewPlugin (
                            te::ExternalPlugin::xmlTypeName, desc);
                        if (pluginPtr != nullptr)
                        {
                            edit.getMasterPluginList().insertPlugin (pluginPtr, slotIndex, nullptr);
                            pluginWindowManager.openPluginWindow (*pluginPtr);
                            refresh();
                        }
                    });
            }
            else
            {
                const int descIdx = result - 1;
                if (descIdx >= 0 && descIdx < descs.size())
                {
                    auto pluginPtr = edit.getPluginCache().createNewPlugin (
                        te::ExternalPlugin::xmlTypeName, descs[descIdx]);
                    if (pluginPtr != nullptr)
                    {
                        edit.getMasterPluginList().insertPlugin (pluginPtr, slotIndex, nullptr);
                        pluginWindowManager.openPluginWindow (*pluginPtr);
                        refresh();
                    }
                }
            }
        });
}

void MasterStrip::MasterInsertSlotPanel::showRemovePluginMenu (te::Plugin* plugin)
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
                plugin->removeFromParent();
                refresh();
            }
        });
}

//==============================================================================
// MasterStrip

MasterStrip::MasterStrip (te::Edit& edit_,
                           MeterProcessor& masterMeter_,
                           PluginWindowManager& pluginWindowManager_,
                           PluginScanner& pluginScanner_)
    : edit (edit_)
    , masterMeter (masterMeter_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner (pluginScanner_)
    , insertPanel (edit_, pluginWindowManager_, pluginScanner_)
{
    // ---- Volume fader ----
    volumeFader.setSliderStyle    (juce::Slider::LinearVertical);
    volumeFader.setTextBoxStyle   (juce::Slider::TextBoxBelow, false, 65, 16);
    volumeFader.setRange          (-60.0, 6.0, 0.1);
    volumeFader.setDoubleClickReturnValue (true, 0.0);
    volumeFader.setTextValueSuffix (" dB");

    if (auto* vp = edit.getMasterVolumePlugin().get())
        volumeFader.setValue (te::volumeFaderPositionToDB (vp->volume.get()),
                              juce::dontSendNotification);

    volumeFader.onValueChange = [this] { volumeChanged(); };

    // ---- Name label ----
    nameLabel.setText ("MASTER", juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, juce::Colour (0xfffffaaa));
    nameLabel.setFont (juce::FontOptions (10.0f));

    addAndMakeVisible (volumeFader);
    addAndMakeVisible (meterDisplay);
    addAndMakeVisible (insertPanel);
    addAndMakeVisible (nameLabel);

    // Listen to master volume plugin state for undo sync
    if (auto* vp = edit.getMasterVolumePlugin().get())
        vp->state.addListener (this);

    // Listen to master plugin list for insert slot refresh
    edit.getMasterPluginList().state.addListener (this);
}

MasterStrip::~MasterStrip()
{
    if (auto* vp = edit.getMasterVolumePlugin().get())
        vp->state.removeListener (this);

    edit.getMasterPluginList().state.removeListener (this);
}

void MasterStrip::resized()
{
    // Layout (bottom → top):
    //   "MASTER" label  18px
    //   Insert slots    96px (6 × 16)
    //   Fader (left) | Meter (22px right) — all remaining height

    auto area = getLocalBounds();

    const int kNameH  = 18;
    const int kSlotH  = MasterInsertSlotPanel::kVisibleSlots * MasterInsertSlotPanel::kSlotHeight;

    nameLabel.setBounds (area.removeFromBottom (kNameH));
    insertPanel.setBounds (area.removeFromBottom (kSlotH));

    // Fader + meter side by side in remaining space
    const int kMeterW = 30;
    auto meterArea = area.removeFromRight (kMeterW);
    volumeFader.setBounds (area);
    meterDisplay.setBounds (meterArea);
}

void MasterStrip::paint (juce::Graphics& g)
{
    // Distinct gold/amber background for the master channel
    g.fillAll (juce::Colour (0xff252018));

    // Gold colour strip at top
    g.setColour (juce::Colour (0xffffc000));
    g.fillRect (0, 0, getWidth(), 4);

    // Right border
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ((float)getWidth() - 1.0f, 0.0f,
                (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);
}

void MasterStrip::setMeterValues (float peakDB, float /*rmsDB*/, float /*lufsM*/, float lufsI)
{
    meterDisplay.setValues (peakDB, lufsI);
}

void MasterStrip::volumeChanged()
{
    auto* vp = edit.getMasterVolumePlugin().get();
    if (vp == nullptr)
        return;

    const float pos = te::decibelsToVolumeFaderPosition ((float)volumeFader.getValue());
    vp->volume.setValue (pos, nullptr);
    vp->setSliderPos (pos);
}

//==============================================================================
// ValueTree::Listener

void MasterStrip::valueTreePropertyChanged (juce::ValueTree& /*tree*/,
                                             const juce::Identifier& property)
{
    // Sync fader position when volume changes externally (undo/redo)
    if (property == te::IDs::volume)
    {
        if (auto* vp = edit.getMasterVolumePlugin().get())
        {
            const float dB = te::volumeFaderPositionToDB (vp->volume.get());
            volumeFader.setValue (dB, juce::dontSendNotification);
        }
    }
}

void MasterStrip::valueTreeChildAdded (juce::ValueTree& /*parent*/, juce::ValueTree& child)
{
    if (child.hasType (juce::Identifier ("PLUGIN")))
        insertPanel.refresh();
}

void MasterStrip::valueTreeChildRemoved (juce::ValueTree& /*parent*/, juce::ValueTree& /*child*/, int)
{
    insertPanel.refresh();
}
