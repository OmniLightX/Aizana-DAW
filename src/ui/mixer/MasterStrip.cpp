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
    // Eindeutige Namen fuer Bedienungshilfen/Automatisierung (Optik unveraendert).
    setTitle (juce::String::fromUTF8 ("Summenkanal Master"));
    volumeFader.setTitle (juce::String::fromUTF8 ("Lautstaerke Master"));
    insertPanel.setTitle (juce::String::fromUTF8 ("Insert-Plaetze Master"));

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
    const int kSlotH  = MasterInsertSlotPanel::kVisibleSlots * MasterInsertSlotPanel::kSlotHeight + InsertRack::kHeaderHeight;

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
    // Sync insert-slot bypass LEDs when a plugin's enabled state changes externally
    // (e.g. toggled from its floating editor window). The change propagates up from
    // the PLUGIN child node to this listener on the master plugin list. Property
    // callbacks may arrive off the message thread (enabled is an atomic CachedValue)
    // → defer the repaint via callAsync + SafePointer.
    else if (property == te::IDs::enabled)
    {
        juce::Component::SafePointer<MasterStrip> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->insertPanel.repaint();
        });
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
