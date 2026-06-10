#include "InspectorPanel.h"

namespace
{
    const juce::Colour kBackground (0xff181818);
    const juce::Colour kBorder     (0xff3a3a3a);
    const juce::Colour kHeadingCol (0xff8aa8c8);
    const juce::Colour kCaptionCol (0xff888888);
    const juce::Colour kValueCol   (0xffdddddd);
}

//==============================================================================
InspectorPanel::InspectorPanel (EditManager& em, MixerEngine& me)
    : editManager (em), mixerEngine (me)
{
    styleHeading (trackHeading, "TRACK");
    styleHeading (eventHeading, "EVENT");

    // ---- Track ----
    styleValue (trackNameLabel, [this] { applyTrackName(); });

    styleCaption (volumeCaption, "Volume");
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.onValueChange = [this]
    {
        if (shownTrack != nullptr)
            mixerEngine.setVolume (*shownTrack, (float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    styleCaption (panCaption, "Pan");
    panSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.onValueChange = [this]
    {
        if (shownTrack != nullptr)
            mixerEngine.setPan (*shownTrack, (float) panSlider.getValue());
    };
    addAndMakeVisible (panSlider);

    // ---- Event / clip ----
    styleValue (clipNameLabel, [this] { applyClipName(); });

    styleCaption (startCaption,   "Start");
    styleCaption (lengthCaption,  juce::String::fromUTF8 ("Länge"));
    styleCaption (fadeInCaption,  "Fade In");
    styleCaption (fadeOutCaption, "Fade Out");
    styleCaption (gainCaption,    "Clip-Gain");

    styleValue (startValue,   [this] { applyStart(); });
    styleValue (lengthValue,  [this] { applyLength(); });
    styleValue (fadeInValue,  [this] { applyFade (true); });
    styleValue (fadeOutValue, [this] { applyFade (false); });

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.onValueChange = [this]
    {
        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (shownClip))
        {
            // Same pathway as the clip's gain handle (te::IDs::gain, undoable)
            wc->state.setProperty (te::IDs::gain, gainSlider.getValue(),
                                   &editManager.getUndoManager());
        }
    };
    addAndMakeVisible (gainSlider);

    emptyHint.setText (juce::String::fromUTF8 ("Track oder Clip auswählen"),
                       juce::dontSendNotification);
    emptyHint.setColour (juce::Label::textColourId, kCaptionCol);
    emptyHint.setFont (juce::FontOptions (12.0f));
    emptyHint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyHint);
}

InspectorPanel::~InspectorPanel() = default;

//==============================================================================
void InspectorPanel::styleHeading (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kHeadingCol);
    l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (l);
}

void InspectorPanel::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kCaptionCol);
    l.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (l);
}

void InspectorPanel::styleValue (juce::Label& l, std::function<void()> onCommit)
{
    l.setEditable (false, true, false);   // double-click to edit (matches TrackHeader)
    l.setColour (juce::Label::textColourId, kValueCol);
    l.setColour (juce::Label::backgroundColourId, juce::Colour (0xff242424));
    l.setFont (juce::FontOptions (12.0f));
    l.onTextChange = std::move (onCommit);
    addAndMakeVisible (l);
}

//==============================================================================
void InspectorPanel::visibilityChanged()
{
    if (isVisible())
    {
        refreshFromSelection();
        startTimer (250);
    }
    else
        stopTimer();
}

void InspectorPanel::timerCallback()
{
    refreshFromSelection();
}

void InspectorPanel::refreshFromSelection()
{
    auto* track = trackProvider ? trackProvider() : nullptr;
    auto* clip  = clipProvider  ? clipProvider()  : nullptr;

    shownTrack = track;
    shownClip  = clip;

    const bool hasTrack = (track != nullptr);
    const bool hasClip  = (clip != nullptr);

    trackHeading.setVisible (hasTrack);
    trackNameLabel.setVisible (hasTrack);
    volumeCaption.setVisible (hasTrack);
    volumeSlider.setVisible (hasTrack);
    panCaption.setVisible (hasTrack);
    panSlider.setVisible (hasTrack);

    const bool clipIsWave = dynamic_cast<te::WaveAudioClip*> (clip) != nullptr;
    eventHeading.setVisible (hasClip);
    clipNameLabel.setVisible (hasClip);
    startCaption.setVisible (hasClip);
    startValue.setVisible (hasClip);
    lengthCaption.setVisible (hasClip);
    lengthValue.setVisible (hasClip);
    fadeInCaption.setVisible (clipIsWave);
    fadeInValue.setVisible (clipIsWave);
    fadeOutCaption.setVisible (clipIsWave);
    fadeOutValue.setVisible (clipIsWave);
    gainCaption.setVisible (clipIsWave);
    gainSlider.setVisible (clipIsWave);

    emptyHint.setVisible (! hasTrack && ! hasClip);

    if (hasTrack) refreshTrackValues();
    if (hasClip)  refreshClipValues();
}

void InspectorPanel::refreshTrackValues()
{
    if (! trackNameLabel.isBeingEdited())
        trackNameLabel.setText (shownTrack->getName(), juce::dontSendNotification);
    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (mixerEngine.getVolume (*shownTrack), juce::dontSendNotification);
    if (! panSlider.isMouseButtonDown())
        panSlider.setValue (mixerEngine.getPan (*shownTrack), juce::dontSendNotification);
}

void InspectorPanel::refreshClipValues()
{
    auto pos = shownClip->getPosition();
    if (! clipNameLabel.isBeingEdited())
        clipNameLabel.setText (shownClip->getName(), juce::dontSendNotification);
    if (! startValue.isBeingEdited())
        startValue.setText (secondsText (pos.getStart().inSeconds()), juce::dontSendNotification);
    if (! lengthValue.isBeingEdited())
        lengthValue.setText (secondsText (pos.getLength().inSeconds()), juce::dontSendNotification);

    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (shownClip))
    {
        if (! fadeInValue.isBeingEdited())
            fadeInValue.setText (secondsText ((double) wc->state.getProperty (te::IDs::fadeIn, 0.0)),
                                 juce::dontSendNotification);
        if (! fadeOutValue.isBeingEdited())
            fadeOutValue.setText (secondsText ((double) wc->state.getProperty (te::IDs::fadeOut, 0.0)),
                                  juce::dontSendNotification);
        if (! gainSlider.isMouseButtonDown())
            gainSlider.setValue ((double) wc->state.getProperty (te::IDs::gain, 0.0),
                                 juce::dontSendNotification);
    }
}

//==============================================================================
void InspectorPanel::applyTrackName()
{
    if (shownTrack == nullptr)
        return;
    const auto name = trackNameLabel.getText().trim();
    if (name.isNotEmpty() && name != shownTrack->getName())
    {
        editManager.getUndoManager().beginNewTransaction ("Rename track");
        shownTrack->setName (name);
    }
}

void InspectorPanel::applyClipName()
{
    if (shownClip == nullptr)
        return;
    const auto name = clipNameLabel.getText().trim();
    if (name.isNotEmpty() && name != shownClip->getName())
    {
        editManager.getUndoManager().beginNewTransaction ("Rename clip");
        shownClip->setName (name);
    }
}

void InspectorPanel::applyStart()
{
    if (shownClip == nullptr)
        return;
    const double newStart = juce::jmax (0.0, parseSeconds (startValue.getText()));
    if (std::abs (newStart - shownClip->getPosition().getStart().inSeconds()) < 0.0005)
        return;
    editManager.getUndoManager().beginNewTransaction ("Clip start");
    // keepLength=true: moving, not trimming (same semantics as a clip drag)
    shownClip->setStart (tracktion::core::TimePosition::fromSeconds (newStart), false, true);
}

void InspectorPanel::applyLength()
{
    if (shownClip == nullptr)
        return;
    const double newLength = juce::jmax (0.05, parseSeconds (lengthValue.getText()));
    if (std::abs (newLength - shownClip->getPosition().getLength().inSeconds()) < 0.0005)
        return;
    editManager.getUndoManager().beginNewTransaction (juce::String::fromUTF8 ("Clip-Länge"));
    shownClip->setLength (tracktion::core::TimeDuration::fromSeconds (newLength), true);
}

void InspectorPanel::applyFade (bool fadeIn)
{
    auto* wc = dynamic_cast<te::WaveAudioClip*> (shownClip);
    if (wc == nullptr)
        return;
    auto& label = fadeIn ? fadeInValue : fadeOutValue;
    const auto prop = fadeIn ? te::IDs::fadeIn : te::IDs::fadeOut;
    const double maxLen = wc->getPosition().getLength().inSeconds();
    const double secs = juce::jlimit (0.0, maxLen, parseSeconds (label.getText()));
    if (std::abs (secs - (double) wc->state.getProperty (prop, 0.0)) < 0.0005)
        return;
    editManager.getUndoManager().beginNewTransaction (fadeIn ? "Fade in" : "Fade out");
    wc->state.setProperty (prop, secs, &editManager.getUndoManager());
}

//==============================================================================
void InspectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);
    g.setColour (kBorder);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    const int rowH = 22, gap = 4;

    auto row = [&] (juce::Component& caption, juce::Component& value)
    {
        auto r = area.removeFromTop (rowH);
        caption.setBounds (r.removeFromLeft (74));
        value.setBounds (r);
        area.removeFromTop (gap);
    };

    trackHeading.setBounds (area.removeFromTop (18));
    area.removeFromTop (gap);
    trackNameLabel.setBounds (area.removeFromTop (rowH));
    area.removeFromTop (gap);
    row (volumeCaption, volumeSlider);
    row (panCaption, panSlider);

    area.removeFromTop (12);
    eventHeading.setBounds (area.removeFromTop (18));
    area.removeFromTop (gap);
    clipNameLabel.setBounds (area.removeFromTop (rowH));
    area.removeFromTop (gap);
    row (startCaption, startValue);
    row (lengthCaption, lengthValue);
    row (fadeInCaption, fadeInValue);
    row (fadeOutCaption, fadeOutValue);
    row (gainCaption, gainSlider);

    emptyHint.setBounds (getLocalBounds().withTrimmedTop (40).withHeight (40));
}
