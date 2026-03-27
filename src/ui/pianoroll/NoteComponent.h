#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class PianoRollView;
class EditManager;

/**
 * NoteComponent — Draggable, resizable note block in the piano roll grid.
 *
 * Renders as a coloured rectangle at the note's pitch/time position.
 * - Body drag: moves note (pitch + beat position)
 * - Right edge drag: resizes note length
 * - Right-click: deletes note
 *
 * Color reflects scale membership:
 *   - In-scale notes: normal teal/green tint
 *   - Out-of-scale notes: red tint (when key highlight mode is on)
 *
 * All mutations route through EditManager::getUndoManager() for undo support.
 */
class NoteComponent : public juce::Component
{
public:
    NoteComponent (te::MidiNote& note,
                   PianoRollView& parent,
                   EditManager& editManager);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    te::MidiNote& getNote() const noexcept { return note; }

    /** Called by PianoRollView when scale settings change so colour can update. */
    void setInScale (bool inScale);

    /** Selection state — highlighted with a brighter fill when selected. */
    void setSelected (bool shouldBeSelected);
    bool isSelected() const noexcept { return selected; }

    /** Callback invoked on left-click so PianoRollView can deselect others first. */
    std::function<void(NoteComponent*)> onSelectionRequest;

private:
    te::MidiNote&  note;
    PianoRollView& parent;
    EditManager&   editManager;

    bool inScale          = true;   // Affects rendering colour
    bool selected         = false;  // Selection state — shown as brighter fill
    bool isDraggingLength = false;  // True when dragging the right edge to resize
    bool isDraggingNote   = false;  // True when dragging the body to move

    // State captured at mouseDown for delta calculations
    juce::Point<float> mouseDownPos;
    double noteStartBeatAtDrag   = 0.0;
    int    notePitchAtDrag       = 0;
    double noteLengthBeatsAtDrag = 0.0;

    static constexpr int kResizeEdgeWidth = 6;  // px — right-edge drag zone

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteComponent)
};
