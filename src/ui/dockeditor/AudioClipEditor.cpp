#include "AudioClipEditor.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using TimeDur = tracktion::core::TimeDuration;

//==============================================================================
AudioClipEditor::AudioClipEditor (te::WaveAudioClip& clip_, EditManager& editManager_)
    : clip (clip_), editManager (editManager_)
{
    // Waveform source — shared app-wide thumbnail cache (finished reads survive
    // component destruction, so reopening the editor repaints instantly).
    const auto audioFile = clip.getSourceFileReference().getFile();
    if (audioFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (audioFile));

    nameLabel.setText (clip.getName(), juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (nameLabel);

    gainCaption.setText ("Gain", juce::dontSendNotification);
    gainCaption.setFont (juce::FontOptions (10.0f));
    gainCaption.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    gainCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (gainCaption);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    syncGainSlider();
    gainSlider.onDragStart = [this]
    {
        editManager.getUndoManager().beginNewTransaction ("Clip gain");
    };
    gainSlider.onValueChange = [this]
    {
        clip.state.setProperty (te::IDs::gain, gainSlider.getValue(),
                                &editManager.getUndoManager());
    };
    addAndMakeVisible (gainSlider);

    zoomInButton.onClick  = [this] { zoomAround (1.0 / 1.4, waveArea().getCentreX()); };
    zoomOutButton.onClick = [this] { zoomAround (1.4,       waveArea().getCentreX()); };
    zoomFitButton.onClick = [this]
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
        syncScrollBar();
        repaint();
    };
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
    addAndMakeVisible (zoomFitButton);

    hScroll.setAutoHide (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    clip.state.addListener (this);
    startTimerHz (30);   // playhead
}

AudioClipEditor::~AudioClipEditor()
{
    clip.state.removeListener (this);
}

//==============================================================================
double AudioClipEditor::clipLenSecs() const
{
    return juce::jmax (0.001, clip.getPosition().getLength().inSeconds());
}

double AudioClipEditor::sourceLenSecs()
{
    const auto info = clip.getAudioFile().getInfo();
    if (info.sampleRate > 0.0)
        return (double) info.lengthInSamples / info.sampleRate;

    // No readable source — fall back to "no extension possible"
    return clipLenSecs() + clip.getPosition().getOffset().inSeconds();
}

juce::Rectangle<int> AudioClipEditor::waveArea() const
{
    auto r = getLocalBounds();
    r.removeFromTop (kHeaderH);
    r.removeFromBottom (kScrollH);
    return r;
}

double AudioClipEditor::xToLocalSecs (float x) const
{
    const auto wa = waveArea();
    if (wa.getWidth() <= 0 || viewLenSecs <= 0.0)
        return 0.0;
    return viewStartSecs + ((double) x - wa.getX()) / wa.getWidth() * viewLenSecs;
}

float AudioClipEditor::localSecsToX (double t) const
{
    const auto wa = waveArea();
    if (viewLenSecs <= 0.0)
        return (float) wa.getX();
    return (float) (wa.getX() + (t - viewStartSecs) / viewLenSecs * wa.getWidth());
}

void AudioClipEditor::clampView()
{
    const double len = clipLenSecs();
    viewLenSecs   = juce::jlimit (0.02, len, viewLenSecs <= 0.0 ? len : viewLenSecs);
    viewStartSecs = juce::jlimit (0.0, juce::jmax (0.0, len - viewLenSecs), viewStartSecs);
}

void AudioClipEditor::zoomAround (double factor, float pivotX)
{
    const double pivotSecs = xToLocalSecs (pivotX);
    viewLenSecs *= factor;
    clampView();
    // Keep the time under the cursor stationary
    const auto wa = waveArea();
    const double frac = wa.getWidth() > 0 ? ((double) pivotX - wa.getX()) / wa.getWidth() : 0.5;
    viewStartSecs = pivotSecs - frac * viewLenSecs;
    clampView();
    syncScrollBar();
    repaint();
}

void AudioClipEditor::syncScrollBar()
{
    hScroll.setRangeLimits ({ 0.0, clipLenSecs() }, juce::dontSendNotification);
    hScroll.setCurrentRange ({ viewStartSecs, viewLenSecs }, juce::dontSendNotification);
}

void AudioClipEditor::syncGainSlider()
{
    const double db = (double) clip.state.getProperty (te::IDs::gain, 0.0);
    gainSlider.setValue (db, juce::dontSendNotification);
}

//==============================================================================
void AudioClipEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (kHeaderH).reduced (6, 3);
    zoomFitButton.setBounds (header.removeFromRight (36));
    zoomInButton .setBounds (header.removeFromRight (24));
    zoomOutButton.setBounds (header.removeFromRight (24));
    header.removeFromRight (10);
    gainSlider.setBounds  (header.removeFromRight (200));
    gainCaption.setBounds (header.removeFromRight (40));
    nameLabel.setBounds   (header);

    hScroll.setBounds (area.removeFromBottom (kScrollH));

    if (viewLenSecs <= 0.0)
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
    }
    syncScrollBar();
}

void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c22));

    const auto wa = waveArea();

    // Header separator
    g.setColour (juce::Colour (0xff000000));
    g.drawHorizontalLine (kHeaderH - 1, 0.0f, (float) getWidth());

    if (viewLenSecs <= 0.0)
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
    }

    // ---- Waveform (big, track-coloured background wash) ----
    g.setColour (clip.getTrack() != nullptr
                     ? clip.getTrack()->getColour().withAlpha (0.14f)
                     : juce::Colour (0xff26262e));
    g.fillRect (wa);

    const double offset = clip.getPosition().getOffset().inSeconds();
    if (thumbnail.getTotalLength() > 0.0)
    {
        const float gainDb  = (float) (double) clip.state.getProperty (te::IDs::gain, 0.0);
        const float gainLin = juce::jlimit (0.0f, 4.0f, juce::Decibels::decibelsToGain (gainDb));

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        thumbnail.drawChannels (g, wa,
                                offset + viewStartSecs,
                                offset + viewStartSecs + viewLenSecs,
                                gainLin);
    }
    else
    {
        g.setColour (juce::Colour (0xff777777));
        g.drawText (juce::String::fromUTF8 ("Keine Audiodatei gefunden — Waveform nicht verf\xc3\xbcgbar"),
                    wa, juce::Justification::centred, false);
    }

    // Centre line
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (wa.getCentreY(), (float) wa.getX(), (float) wa.getRight());

    // ---- Fade overlays (same property semantics as ClipComponent) ----
    const double fadeIn  = (double) clip.state.getProperty (te::IDs::fadeIn,  0.0);
    const double fadeOut = (double) clip.state.getProperty (te::IDs::fadeOut, 0.0);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    if (fadeIn > 0.0)
    {
        juce::Path p;
        const float x0 = localSecsToX (0.0), x1 = localSecsToX (fadeIn);
        p.startNewSubPath (x0, (float) wa.getY());
        p.lineTo (x1, (float) wa.getY());
        p.lineTo (x0, (float) wa.getBottom());
        p.closeSubPath();
        g.fillPath (p);
    }
    if (fadeOut > 0.0)
    {
        juce::Path p;
        const double len = clipLenSecs();
        const float x0 = localSecsToX (len - fadeOut), x1 = localSecsToX (len);
        p.startNewSubPath (x1, (float) wa.getY());
        p.lineTo (x0, (float) wa.getY());
        p.lineTo (x1, (float) wa.getBottom());
        p.closeSubPath();
        g.fillPath (p);
    }

    // Fade handles (top corners at the fade boundary)
    g.setColour (juce::Colour (0xff44aaff));
    g.fillEllipse (localSecsToX (fadeIn) - 5.0f,               (float) wa.getY() + 3.0f, 10.0f, 10.0f);
    g.fillEllipse (localSecsToX (clipLenSecs() - fadeOut) - 5.0f, (float) wa.getY() + 3.0f, 10.0f, 10.0f);

    // ---- Trim edge zones (visible when the clip edge is inside the view) ----
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    if (viewStartSecs <= 0.001)
        g.fillRect (wa.getX(), wa.getY(), 3, wa.getHeight());
    if (viewStartSecs + viewLenSecs >= clipLenSecs() - 0.001)
        g.fillRect (wa.getRight() - 3, wa.getY(), 3, wa.getHeight());

    // ---- Playhead ----
    if (auto* edit = editManager.getEdit())
    {
        const double tpos  = edit->getTransport().getPosition().inSeconds();
        const double local = tpos - clip.getPosition().getStart().inSeconds();
        if (local >= viewStartSecs && local <= viewStartSecs + viewLenSecs)
        {
            g.setColour (juce::Colours::white);
            const float x = localSecsToX (local);
            g.drawLine (x, (float) wa.getY(), x, (float) wa.getBottom(), 1.5f);
        }
    }
}

//==============================================================================
void AudioClipEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto wa = waveArea();
    if (! wa.contains (e.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    const bool nearLeft  = e.x <= wa.getX() + kEdgeZone     && viewStartSecs <= 0.001;
    const bool nearRight = e.x >= wa.getRight() - kEdgeZone && viewStartSecs + viewLenSecs >= clipLenSecs() - 0.001;
    setMouseCursor ((nearLeft || nearRight) ? juce::MouseCursor::LeftRightResizeCursor
                                            : juce::MouseCursor::IBeamCursor);
}

void AudioClipEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto wa = waveArea();
    if (! wa.contains (e.getPosition()))
        return;

    const double fadeIn  = (double) clip.state.getProperty (te::IDs::fadeIn,  0.0);
    const double fadeOut = (double) clip.state.getProperty (te::IDs::fadeOut, 0.0);
    const double len     = clipLenSecs();

    dragStartScreenX    = e.getScreenX();
    dragStartClipStart  = clip.getPosition().getStart().inSeconds();
    dragStartClipEnd    = clip.getPosition().getEnd().inSeconds();
    dragStartOffset     = clip.getPosition().getOffset().inSeconds();

    // Fade handles first (they sit at the top edge)
    if (e.y < wa.getY() + kFadeGrab + 4)
    {
        if (std::abs (e.x - localSecsToX (fadeIn)) <= kFadeGrab)
        {
            activeDrag = Drag::fadeIn;
            dragStartFadeSecs = fadeIn;
            editManager.getUndoManager().beginNewTransaction ("Fade in");
            return;
        }
        if (std::abs (e.x - localSecsToX (len - fadeOut)) <= kFadeGrab)
        {
            activeDrag = Drag::fadeOut;
            dragStartFadeSecs = fadeOut;
            editManager.getUndoManager().beginNewTransaction ("Fade out");
            return;
        }
    }

    // Trim zones (only when the actual clip edge is visible)
    if (e.x <= wa.getX() + kEdgeZone && viewStartSecs <= 0.001)
    {
        activeDrag = Drag::trimLeft;
        trimMaxExtendLeft = dragStartClipStart - dragStartOffset;   // can't reveal before source start
        editManager.getUndoManager().beginNewTransaction ("Trim clip");
        return;
    }
    if (e.x >= wa.getRight() - kEdgeZone && viewStartSecs + viewLenSecs >= len - 0.001)
    {
        activeDrag = Drag::trimRight;
        trimMaxExtendRight = dragStartClipStart + (sourceLenSecs() - dragStartOffset);
        editManager.getUndoManager().beginNewTransaction ("Trim clip");
        return;
    }

    // Plain click = seek transport to that position
    if (auto* edit = editManager.getEdit())
    {
        const double local = juce::jlimit (0.0, len, xToLocalSecs ((float) e.x));
        edit->getTransport().setPosition (TimePos::fromSeconds (dragStartClipStart + local));
        repaint();
    }
}

void AudioClipEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDrag == Drag::none)
        return;

    const auto wa = waveArea();
    const double pxPerSec = viewLenSecs > 0.0 ? wa.getWidth() / viewLenSecs : 1.0;
    const double deltaSecs = (e.getScreenX() - dragStartScreenX) / juce::jmax (1.0, pxPerSec);
    auto& um = editManager.getUndoManager();

    if (activeDrag == Drag::fadeIn)
    {
        const double len = clipLenSecs();
        const double f = juce::jlimit (0.0, len, dragStartFadeSecs + deltaSecs);
        clip.state.setProperty (te::IDs::fadeIn, f, &um);
        repaint();
        return;
    }
    if (activeDrag == Drag::fadeOut)
    {
        const double len = clipLenSecs();
        const double f = juce::jlimit (0.0, len, dragStartFadeSecs - deltaSecs);
        clip.state.setProperty (te::IDs::fadeOut, f, &um);
        repaint();
        return;
    }
    if (activeDrag == Drag::trimLeft)
    {
        // Left trim keeps source sync: offset moves with the start (ClipComponent semantics)
        const double newStart = juce::jlimit (trimMaxExtendLeft,
                                              dragStartClipEnd - 0.05,
                                              dragStartClipStart + deltaSecs);
        const double newOffset = dragStartOffset + (newStart - dragStartClipStart);
        clip.setPosition ({ TimeRng (TimePos::fromSeconds (newStart),
                                     TimePos::fromSeconds (dragStartClipEnd)),
                            TimeDur::fromSeconds (newOffset) });
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
        syncScrollBar();
        repaint();
        return;
    }
    if (activeDrag == Drag::trimRight)
    {
        const double newEnd = juce::jlimit (dragStartClipStart + 0.05,
                                            trimMaxExtendRight,
                                            dragStartClipEnd + deltaSecs);
        clip.setPosition ({ TimeRng (TimePos::fromSeconds (dragStartClipStart),
                                     TimePos::fromSeconds (newEnd)),
                            TimeDur::fromSeconds (dragStartOffset) });
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
        syncScrollBar();
        repaint();
        return;
    }
}

void AudioClipEditor::mouseUp (const juce::MouseEvent&)
{
    activeDrag = Drag::none;
}

void AudioClipEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        // Cmd+scroll = zoom around cursor (same convention as the arrangement)
        zoomAround (wheel.deltaY > 0 ? 1.0 / 1.15 : 1.15, (float) e.x);
        return;
    }

    // Plain scroll = horizontal pan
    const double step = (wheel.deltaX != 0.0f ? -wheel.deltaX : -wheel.deltaY) * viewLenSecs * 0.25;
    viewStartSecs += step;
    clampView();
    syncScrollBar();
    repaint();
}

void AudioClipEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        zoomAround (1.0 / (double) scaleFactor, (float) e.x);
}

//==============================================================================
void AudioClipEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop)
{
    // May fire off the message thread (undo etc.) — defer UI work.
    juce::Component::SafePointer<AudioClipEditor> safe (this);
    juce::MessageManager::callAsync ([safe, prop]
    {
        if (safe == nullptr)
            return;
        if (prop == te::IDs::gain)
            safe->syncGainSlider();
        safe->clampView();
        safe->syncScrollBar();
        safe->repaint();
    });
}

void AudioClipEditor::timerCallback()
{
    // Cheap playhead-only repaint while playing
    if (auto* edit = editManager.getEdit())
        if (edit->getTransport().isPlaying())
            repaint (waveArea());
}

void AudioClipEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewStartSecs = newRangeStart;
    clampView();
    repaint();
}
