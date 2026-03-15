#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"

namespace te = tracktion::engine;

class PianoRollView;

/**
 * VelocityLane — Shows note velocities as vertical bars below the piano roll grid.
 *
 * Fixed height of ~80px. One vertical bar per note, x-aligned to note start,
 * height proportional to velocity (0–127 → 0%–100% of lane height).
 *
 * Interaction:
 *   - Click/drag on or near a bar: changes that note's velocity via UndoManager.
 *   - Bar colour matches note selection state (selected = highlighted).
 *
 * Horizontal scroll position must be kept in sync with PianoRollView (the parent
 * passes its scroll offset so bars align with note grid columns).
 */
class VelocityLane : public juce::Component
{
public:
    VelocityLane (te::MidiClip& clip,
                  PianoRollView& pianoRoll,
                  EditManager& editManager);

    void paint     (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    /** Called by PianoRollView when zoom or scroll changes so bars stay aligned. */
    void repaintLane();

private:
    te::MidiClip& clip;
    PianoRollView& pianoRoll;
    EditManager&   editManager;

    te::MidiNote* draggedNote = nullptr;  // Note being velocity-edited

    /** Find the note closest to the given x position (in lane-local coords). */
    te::MidiNote* noteAtX (float x) const;

    /** Set velocity of note to newVelocity [0,1] range through UndoManager. */
    void setNoteVelocity (te::MidiNote* note, float newVelocity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityLane)
};
