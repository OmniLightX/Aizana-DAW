#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "NoteComponent.h"
#include "engine/EditManager.h"

// Forward-declare sub-lane types to break circular include dependency.
// PianoRollView.h uses unique_ptr<VelocityLane> and unique_ptr<CCLane>;
// their destructors are instantiated in PianoRollView.cpp where both headers are included.
class VelocityLane;
class CCLane;

namespace te = tracktion::engine;

/**
 * PianoRollView — Full MIDI note editor for a single MidiClip.
 *
 * Layout:
 *   [Header: bar/beat markers + key selector + scale selector + quantize grid selector]
 *   [Piano keyboard (40px) | Note grid (scrollable, pitch × time)                    ]
 *   [                      | VelocityLane (80px, scrolls with grid)                  ]
 *   [                      | CCLane (60px, scrolls with grid)                        ]
 *   [                      | Takes/Comping rows (only if clip has > 1 take)          ]
 *
 * Features (all per CONTEXT.md):
 *   - Draw note: left-click empty space; quantized to selected grid division
 *   - Delete note: right-click on NoteComponent
 *   - Move note: drag body of NoteComponent
 *   - Resize note: drag right edge of NoteComponent
 *   - Velocity editing: VelocityLane (drag bars)
 *   - CC lane: CCLane shows CC7 Volume by default; draw CC events
 *   - Scale/key locking: highlight mode (default) or prevent mode
 *   - Zoom: horizontal (pixelsPerBeat) + vertical (rowHeight)
 *   - MIDI comping: take rows visible when clip has multiple takes
 *
 * Reactive to MidiClip ValueTree: rebuilds NoteComponents on note add/remove.
 */
class PianoRollView : public juce::Component,
                      private juce::ValueTree::Listener,
                      private juce::ScrollBar::Listener
{
public:
    explicit PianoRollView (te::MidiClip& clip, EditManager& editManager);
    ~PianoRollView() override;

    void resized() override;
    void paint   (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    // ---- Zoom (called by Ctrl+scroll or pinch gesture) ----
    void setHorizontalZoom (double pixelsPerBeat);
    void setVerticalZoom   (double rowHeightPixels);

    // ---- Scale / key locking (CONTEXT.md: "wrong notes prevented or highlighted") ----
    void setKey (int rootNote, const juce::Array<int>& intervals);
    void setKeyLockEnabled (bool prevent);  // true=prevent, false=highlight only

    // ---- Coordinate helpers (used by NoteComponent + VelocityLane + CCLane) ----
    float  xAtBeat  (double beat)  const noexcept;
    float  yAtPitch (int pitch)    const noexcept;
    double beatAtX  (float x)      const noexcept;
    int    pitchAtY (float y)      const noexcept;
    double pixelsPerBeat() const noexcept { return ppb; }
    double rowHeightPx()   const noexcept { return rowH; }
    double scrollBeatOffset() const noexcept { return scrollBeat; }

    bool isInScale (int pitch) const noexcept;

    // ---- Note mutation (called by NoteComponent for drag operations) ----
    void moveNote   (te::MidiNote& note, int newPitch, double newStartBeat);
    void resizeNote (te::MidiNote& note, double newLengthBeats);
    void removeNote (te::MidiNote& note);

private:
    te::MidiClip& clip;
    EditManager&  editManager;

    // ---- Grid parameters ----
    double ppb      = 80.0;   // pixels per beat
    double rowH     = 16.0;   // row height in pixels (one semitone per row)
    double scrollBeat  = 0.0; // horizontal scroll position (in beats)
    int    scrollPitch = 60;  // vertical scroll — MIDI pitch at top row

    static constexpr int kKeyboardWidth = 40;
    static constexpr int kHeaderHeight  = 32;
    static constexpr int kTotalPitches  = 128;

    // ---- Scale / key locking ----
    int              selectedKey    = 0;                      // C
    juce::Array<int> scaleIntervals = {0,2,4,5,7,9,11};      // Major by default
    bool             keyLockEnabled = false;                  // highlight mode default

    // ---- Controls in header ----
    juce::ComboBox keySelector;     // Root note C..B
    juce::ComboBox scaleSelector;   // Major, Minor, Dorian, etc.
    juce::ComboBox quantizeGrid;    // 1/4, 1/8, 1/16, 1/32

    // ---- Quantize-Bar (E7, S1-Stil): Grid (Combo oben) + Strength/Swing + Apply ----
    juce::Slider     qStrength;     // 0..100 % (default 100)
    juce::Slider     qSwing;        // 0..100 % (default 0)
    juce::TextButton qApplyBtn { "Q" };
    void applyQuantize();           // Selektion, sonst alle Noten — 1 Undo

    // ---- Scroll bars ----
    juce::ScrollBar hScrollBar { false };  // horizontal
    juce::ScrollBar vScrollBar { true  };  // vertical

    // ---- Sub-lanes ----
    std::unique_ptr<VelocityLane> velocityLane;
    std::unique_ptr<CCLane>       ccLane;

    // ---- Note children ----
    juce::OwnedArray<NoteComponent> noteComponents;

    // ---- Piano keyboard sub-component ----
    class PianoKeyboard : public juce::Component
    {
    public:
        explicit PianoKeyboard (PianoRollView& owner) : owner (owner) {}

        void paint     (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp   (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

    private:
        PianoRollView& owner;
        int activePreviewNote = -1;   // pitch currently sounding, -1 = none

        void startPreviewNote (int pitch);
        void stopPreviewNote();
        int  pitchFromY (float y) const noexcept;
    };
    PianoKeyboard pianoKeyboard { *this };

    // ---- Takes/comping panel ----
    class TakesPanel : public juce::Component
    {
    public:
        TakesPanel (te::MidiClip& clip, EditManager& editManager);
        void paint  (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void rebuild();
    private:
        te::MidiClip& clip;
        EditManager&  editManager;
        juce::OwnedArray<juce::TextButton> takeButtons;
    };
    std::unique_ptr<TakesPanel> takesPanel;

    // ---- MIDI preview (used by PianoKeyboard mouse handlers) ----
    void sendPreviewNoteOn  (int pitch, int velocity = 100);
    void sendPreviewNoteOff (int pitch);

    // ---- Selection ----
    void selectAll();
    void deselectAll();
    void deleteSelectedNotes();
    void updateMarqueeSelection();

    juce::Rectangle<float> marqueeRect;       // current rubber-band rect in component coords
    bool                   isMarqueeActive = false;
    juce::Point<float>     marqueeStart;

    // ---- Private helpers ----
    void addNote (double beatPosition, int pitch, float velocity = 0.75f);
    void rebuildNoteComponents();
    void updateNoteComponentBounds (NoteComponent& nc);
    void updateAllNoteScaleColors();
    double quantizeBeat (double rawBeat) const noexcept;
    int    snapToScale  (int pitch)      const noexcept;
    int    getGridDivisionDenominator() const noexcept;

    void scrollBarMoved (juce::ScrollBar*, double newScrollPos) override;

    // ValueTree::Listener — note add/remove in clip's MidiList
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override {}
    void valueTreeParentChanged   (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
