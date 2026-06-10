#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "engine/CompingEngine.h"

namespace te = tracktion::engine;

/**
 * CompingView — Studio-One-style take comping editor for one WaveAudioClip.
 *
 * Docked at the bottom of the main window (same slot as the Piano Roll).
 * One lane per loop take, waveform thumbnails, and the active comp drawn as
 * highlighted regions on the lane the audio comes from:
 *
 *   [Header: "Comping — <clip>"  hint   [Comp auflösen]              ]
 *   [ Take 1 | ~~~waveform~~~  ████████░░░░░░░░░░░░░░░░░░░░░░        ]
 *   [ Take 2 | ~~~waveform~~~  ░░░░░░░░████████████░░░░░░░░░░        ]
 *   [ Take 3 | ~~~waveform~~~  ░░░░░░░░░░░░░░░░░░░░██████████        ]
 *
 * Interaction (CompingEngine does the model work, 1 undo step per gesture):
 *   - Drag across a lane  → that range comes from that take
 *   - Click on a lane     → the section under the mouse switches to that take
 *   - Click on lane label → the whole clip uses that take (comp removed)
 *
 * The view holds a Clip::Ptr (refcount) and listens to the clip state, so
 * undo/redo repaints and deleting the clip closes the editor via onCloseRequest.
 */
class CompingView : public juce::Component,
                    private juce::ValueTree::Listener
{
public:
    CompingView (te::WaveAudioClip& clip, EditManager& em, CompingEngine& engine);
    ~CompingView() override;

    /** Set by MainComponent — called when the clip disappears (delete/undo). */
    std::function<void()> onCloseRequest;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    te::WaveAudioClip* getClip() const;

private:
    te::Clip::Ptr clipPtr;                 // keeps the clip alive while open
    EditManager& editManager;
    CompingEngine& compingEngine;

    juce::TextButton clearCompButton;

    // Lane geometry
    static constexpr int kHeaderHeight = 28;
    static constexpr int kLabelWidth   = 140;
    int numTakes = 0;
    double compLength = 0.0;               // seconds (cached; takes don't change while open)

    // Waveform thumbnails — one per take, sharing one cache (anti-pattern note:
    // per-view cache is fine here, the view exists once and holds few thumbnails)
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 16 };
    juce::OwnedArray<juce::AudioThumbnail> thumbnails;
    juce::Array<juce::File> takeFiles;

    // Drag gesture state
    int dragLane = -1;
    double dragStartTime = 0.0, dragCurrentTime = 0.0;
    bool dragging = false;

    juce::Rectangle<int> getLaneArea() const;       // all lanes (below header)
    juce::Rectangle<int> getLaneBounds (int laneIndex) const;
    int laneIndexAt (juce::Point<int>) const;       // -1 when outside
    double xToTime (int x) const;
    int timeToX (double t) const;

    void rebuildThumbnails();

    // juce::ValueTree::Listener — repaint on comp/source changes, close on delete
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaint(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaint(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaint(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              {}
    void valueTreeParentChanged (juce::ValueTree&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompingView)
};
