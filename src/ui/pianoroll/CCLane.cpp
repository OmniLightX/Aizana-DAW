#include "CCLane.h"
#include "PianoRollView.h"

//==============================================================================

CCLane::CCLane (te::MidiClip& clip_,
                int controllerNumber_,
                PianoRollView& pianoRoll_,
                EditManager& editManager_)
    : clip (clip_),
      ccNumber (controllerNumber_),
      pianoRoll (pianoRoll_),
      editManager (editManager_)
{
    // CC selector combo box
    ccSelector.addItem ("CC7 Volume",     7);
    ccSelector.addItem ("CC10 Pan",       10);
    ccSelector.addItem ("CC11 Expression", 11);
    ccSelector.addItem ("CC1 Modulation", 1);
    ccSelector.addItem ("CC64 Sustain",   64);
    ccSelector.setSelectedId (ccNumber, juce::dontSendNotification);
    ccSelector.onChange = [this]
    {
        ccNumber = ccSelector.getSelectedId();
        repaint();
    };
    addAndMakeVisible (ccSelector);

    setRepaintsOnMouseActivity (true);
}

//==============================================================================

void CCLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int  h      = bounds.getHeight();
    const int  w      = bounds.getWidth();

    // Background
    g.fillAll (juce::Colour (0xff161616));

    // Top separator
    g.setColour (juce::Colour (0xff444444));
    g.drawLine (0.0f, 0.0f, static_cast<float> (w), 0.0f, 1.0f);

    // CC data drawing area starts after the label panel
    const juce::Rectangle<int> drawArea (kLabelWidth, 0, w - kLabelWidth, h);
    g.reduceClipRegion (drawArea);

    // Draw CC events from the MidiClip sequence
    auto& seq = clip.getSequence();
    const int numEvents = seq.getNumControllerEvents();

    for (int i = 0; i < numEvents; ++i)
    {
        const auto* evt = seq.getControllerEvent (i);
        if (evt == nullptr || evt->getType() != ccNumber)
            continue;

        // xAtBeat returns PianoRollView-local x; CCLane is offset by kKeyboardWidth (40px)
        // within PianoRollView, and the drawing area starts at kLabelWidth within CCLane.
        const float x = pianoRoll.xAtBeat (evt->getBeatPosition().inBeats())
                        - 40.0f + kLabelWidth;

        if (x < kLabelWidth || x > static_cast<float> (w))
            continue;

        const float valNorm = static_cast<float> (evt->getControllerValue()) / 127.0f;
        const float barH    = valNorm * static_cast<float> (h - 4);
        const float barY    = static_cast<float> (h) - barH - 2.0f;
        const float barW    = juce::jmax (2.0f, static_cast<float> (pianoRoll.pixelsPerBeat() * 0.06));

        g.setColour (juce::Colour (0xff6a8fcc));
        g.fillRect  (juce::Rectangle<float> (x - barW * 0.5f, barY, barW, barH));
    }

    // Label
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("CC", 2, 2, 30, 12, juce::Justification::centredLeft, false);
}

void CCLane::resized()
{
    ccSelector.setBounds (0, 0, kLabelWidth - 2, getHeight());
}

//==============================================================================

void CCLane::setCC (double beatPos, int value)
{
    auto& seq = clip.getSequence();
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction();

    // Add controller event at the given beat position
    seq.addControllerEvent (tracktion::core::BeatPosition::fromBeats (beatPos),
                            ccNumber,
                            juce::jlimit (0, 127, value),
                            &undoManager);
    repaint();
}

//==============================================================================

void CCLane::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < kLabelWidth) return;

    // Convert CCLane local x to PianoRollView local x (CCLane starts at kKeyboardWidth=40 in PRV)
    const float prvX = static_cast<float> (e.x - kLabelWidth + 40);
    const double beatPos = pianoRoll.beatAtX (prvX);
    const int value = juce::jlimit (0, 127,
                                    127 - static_cast<int> (static_cast<float> (e.y) / static_cast<float> (getHeight()) * 127.0f));
    setCC (beatPos, value);
}

void CCLane::mouseDrag (const juce::MouseEvent& e)
{
    if (e.x < kLabelWidth) return;

    const float prvX = static_cast<float> (e.x - kLabelWidth + 40);
    const double beatPos = pianoRoll.beatAtX (prvX);
    const int value = juce::jlimit (0, 127,
                                    127 - static_cast<int> (static_cast<float> (e.y) / static_cast<float> (getHeight()) * 127.0f));
    setCC (beatPos, value);
}

void CCLane::mouseUp (const juce::MouseEvent&)
{
    repaint();
}

void CCLane::setControllerNumber (int cc)
{
    ccNumber = cc;
    ccSelector.setSelectedId (cc, juce::dontSendNotification);
    repaint();
}

void CCLane::repaintLane()
{
    repaint();
}
