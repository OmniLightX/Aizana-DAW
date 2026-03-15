#include "VelocityLane.h"
#include "PianoRollView.h"

//==============================================================================

VelocityLane::VelocityLane (te::MidiClip& clip_,
                              PianoRollView& pianoRoll_,
                              EditManager& editManager_)
    : clip (clip_),
      pianoRoll (pianoRoll_),
      editManager (editManager_)
{
    setRepaintsOnMouseActivity (true);
}

//==============================================================================

void VelocityLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int  h      = bounds.getHeight();
    const int  w      = bounds.getWidth();

    // Background
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Top separator
    g.setColour (juce::Colour (0xff444444));
    g.drawLine (0.0f, 0.0f, static_cast<float> (w), 0.0f, 1.0f);

    // Draw velocity bars for each note
    auto& midiList = clip.getSequence();
    const int numNotes = midiList.getNumNotes();

    for (int i = 0; i < numNotes; ++i)
    {
        const auto* note = midiList.getNote (i);
        if (note == nullptr)
            continue;

        // xAtBeat returns coords in PianoRollView space; lane starts at kKeyboardWidth
        // so subtract keyboard width to get lane-local x
        const float x       = pianoRoll.xAtBeat (note->getStartBeat().inBeats()) - 40.0f;
        const float barW    = juce::jmax (2.0f, static_cast<float> (pianoRoll.pixelsPerBeat() * 0.08));
        const float velNorm = static_cast<float> (note->getVelocity()) / 127.0f;
        const float barH    = velNorm * static_cast<float> (h - 4);

        if (x < 0.0f || x > static_cast<float> (w))
            continue;

        // Bar: bottom-anchored
        const float barY = static_cast<float> (h) - barH - 2.0f;

        // Highlighted if being dragged
        const bool  isDragged = (draggedNote == note);
        const juce::Colour barColour = isDragged
            ? juce::Colour (0xffffa500)    // orange when dragging
            : juce::Colour (0xff1fa882);   // teal default

        g.setColour (barColour);
        g.fillRect  (juce::Rectangle<float> (x - barW * 0.5f, barY, barW, barH));

        // Velocity value label at top when dragging
        if (isDragged)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (juce::String (note->getVelocity()),
                        static_cast<int> (x) - 12, static_cast<int> (barY) - 12,
                        24, 12, juce::Justification::centred, false);
        }
    }

    // Label
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("VEL", 2, 2, 30, 12, juce::Justification::centredLeft, false);
}

//==============================================================================

te::MidiNote* VelocityLane::noteAtX (float x) const
{
    auto& midiList = clip.getSequence();
    const int numNotes = midiList.getNumNotes();

    te::MidiNote* closest = nullptr;
    float closestDist = 20.0f; // px threshold

    for (int i = 0; i < numNotes; ++i)
    {
        auto* note = midiList.getNote (i);
        if (note == nullptr) continue;

        const float noteX = pianoRoll.xAtBeat (note->getStartBeat().inBeats()) - 40.0f;
        const float dist = std::abs (x - noteX);

        if (dist < closestDist)
        {
            closestDist = dist;
            closest     = note;
        }
    }
    return closest;
}

void VelocityLane::setNoteVelocity (te::MidiNote* note, float newVelocity)
{
    if (note == nullptr) return;

    const int vel = juce::jlimit (1, 127, static_cast<int> (newVelocity * 127.0f));
    auto& undoManager = editManager.getUndoManager();

    note->setVelocity (vel, &undoManager);
    repaint();
}

//==============================================================================

void VelocityLane::mouseDown (const juce::MouseEvent& e)
{
    draggedNote = noteAtX (static_cast<float> (e.x));
    if (draggedNote != nullptr)
    {
        editManager.getUndoManager().beginNewTransaction();

        const float velNorm = 1.0f - static_cast<float> (e.y) / static_cast<float> (getHeight());
        setNoteVelocity (draggedNote, juce::jlimit (0.0f, 1.0f, velNorm));
    }
}

void VelocityLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedNote == nullptr)
        draggedNote = noteAtX (static_cast<float> (e.x));

    if (draggedNote != nullptr)
    {
        const float velNorm = 1.0f - static_cast<float> (e.y) / static_cast<float> (getHeight());
        setNoteVelocity (draggedNote, juce::jlimit (0.0f, 1.0f, velNorm));
    }
}

void VelocityLane::mouseUp (const juce::MouseEvent&)
{
    draggedNote = nullptr;
    repaint();
}

void VelocityLane::repaintLane()
{
    repaint();
}
