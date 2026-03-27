#include "NoteComponent.h"
#include "PianoRollView.h"
#include "engine/EditManager.h"

//==============================================================================

NoteComponent::NoteComponent (te::MidiNote& note_,
                               PianoRollView& parent_,
                               EditManager& editManager_)
    : note        (note_),
      parent      (parent_),
      editManager (editManager_)
{
    setRepaintsOnMouseActivity (true);
}

void NoteComponent::setInScale (bool inScaleFlag)
{
    if (inScale != inScaleFlag)
    {
        inScale = inScaleFlag;
        repaint();
    }
}

void NoteComponent::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

//==============================================================================

void NoteComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Base colour: teal for in-scale, red-tinted for out-of-scale
    juce::Colour baseColour = inScale
        ? juce::Colour (0xff1fa882)   // teal
        : juce::Colour (0xffb84040);  // muted red — out of scale

    // Brighter when selected
    if (selected)
        baseColour = baseColour.brighter (0.5f);

    // Slightly brighter when mouse-over
    if (isMouseOver())
        baseColour = baseColour.brighter (0.15f);

    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds.reduced (0.5f), 2.0f);

    // Highlight line at top
    g.setColour (baseColour.brighter (0.4f));
    g.drawLine (bounds.getX() + 2.0f, bounds.getY() + 1.0f,
                bounds.getRight() - 2.0f, bounds.getY() + 1.0f, 1.0f);

    // Border
    g.setColour (baseColour.darker (0.4f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 2.0f, 1.0f);

    // Draw resize handle indicator on right edge
    const float edgeX = bounds.getRight() - kResizeEdgeWidth;
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (edgeX, bounds.getY() + 2.0f,
                                        2.0f, bounds.getHeight() - 4.0f));
}

//==============================================================================

void NoteComponent::mouseDown (const juce::MouseEvent& e)
{
    // Right-click = delete note
    if (e.mods.isRightButtonDown())
    {
        parent.removeNote (note);
        return;
    }

    mouseDownPos = e.position;
    noteStartBeatAtDrag   = note.getStartBeat().inBeats();
    notePitchAtDrag       = note.getNoteNumber();
    noteLengthBeatsAtDrag = note.getLengthBeats().inBeats();

    // Determine drag mode: right edge = resize, body = move
    isDraggingLength = (e.x >= getWidth() - kResizeEdgeWidth);
    isDraggingNote   = !isDraggingLength;

    // Left-click selects this note (callback lets PianoRollView deselect others)
    if (e.mods.isLeftButtonDown() && !e.mods.isCommandDown())
    {
        if (onSelectionRequest)
            onSelectionRequest (this);
    }

    editManager.getUndoManager().beginNewTransaction();
}

void NoteComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
        return;

    const float dx = e.position.x - mouseDownPos.x;
    const float dy = e.position.y - mouseDownPos.y;

    if (isDraggingLength)
    {
        // Resize: delta x → delta beats
        const double deltaBeat = dx / parent.pixelsPerBeat();
        const double newLength = juce::jmax (0.0625, noteLengthBeatsAtDrag + deltaBeat);
        parent.resizeNote (note, newLength);
    }
    else if (isDraggingNote)
    {
        // Move: delta x → beat offset, delta y → pitch offset
        const double deltaBeat  = dx / parent.pixelsPerBeat();
        const int    deltaPitch = -static_cast<int> (std::round (dy / parent.rowHeightPx()));

        const double newBeat  = juce::jmax (0.0, noteStartBeatAtDrag + deltaBeat);
        const int    newPitch = juce::jlimit (0, 127, notePitchAtDrag + deltaPitch);

        parent.moveNote (note, newPitch, newBeat);
    }
}

void NoteComponent::mouseUp (const juce::MouseEvent&)
{
    isDraggingLength = false;
    isDraggingNote   = false;
}
