#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"

namespace te = tracktion::engine;

class PianoRollView;

/**
 * CCLane — Shows MIDI controller (CC) automation data below the VelocityLane.
 *
 * Height ~60px (expandable in future phases). Displays one CC controller's events
 * as vertical bars. Default controller: CC7 (Volume).
 *
 * Left side shows a CC number selector label. Right side is the drawing area.
 *
 * Interaction:
 *   - Click on empty area: adds a CC event at that beat position and value.
 *   - Drag: sets CC value at the drag position continuously.
 *   - CC events are stored in the MidiClip's sequence via addControllerEvent().
 *
 * Horizontal scroll position is driven by PianoRollView to stay in sync.
 */
class CCLane : public juce::Component
{
public:
    CCLane (te::MidiClip& clip,
            int controllerNumber,
            PianoRollView& pianoRoll,
            EditManager& editManager);

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    void setControllerNumber (int cc);
    int  getControllerNumber() const noexcept { return ccNumber; }

    /** Called by PianoRollView when zoom/scroll changes. */
    void repaintLane();

private:
    te::MidiClip& clip;
    int           ccNumber;
    PianoRollView& pianoRoll;
    EditManager&   editManager;

    juce::ComboBox ccSelector;   // "CC7 Volume", "CC10 Pan", "CC11 Expression", etc.
    static constexpr int kLabelWidth = 80;  // px — CC selector panel on left

    /** Add or update a CC event at beatPos with value [0,127] via UndoManager. */
    void setCC (double beatPos, int value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CCLane)
};
