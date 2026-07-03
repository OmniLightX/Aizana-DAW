#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "ui/pianoroll/PianoRollView.h"
#include "AudioClipEditor.h"

namespace te = tracktion::engine;

/**
 * DockedEditorPanel — the Studio One style "Edit" view for the bottom dock.
 *
 * Shows an editor for the CURRENT GLOBAL SELECTION (no separate editor
 * selection, no copied clip data):
 *
 *   - WaveAudioClip selected → AudioClipEditor (big waveform)
 *   - MidiClip selected      → PianoRollView (existing component, reused)
 *   - nothing selected       → empty state text
 *
 * Selection sync: polls the selection provider on a timer (same pattern the
 * InspectorPanel uses on getPrimarySelectedClip()) — arrangement selection
 * changes appear here within one tick, and all edits go straight to the live
 * clip objects through the shared UndoManager.
 */
class DockedEditorPanel : public juce::Component,
                          private juce::Timer
{
public:
    DockedEditorPanel (EditManager& editManager,
                       std::function<te::Clip*()> selectionProvider);
    ~DockedEditorPanel() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Show a specific clip immediately (e.g. double-click on a MIDI clip)
        without waiting for the next poll tick. */
    void showClip (te::Clip* clip);

    /** Drop all clip references (edit swap / dock hidden). */
    void clear();

    /** While hidden the poll timer pauses; call when the dock opens/closes. */
    void setDockActive (bool active);

private:
    EditManager& editManager;
    std::function<te::Clip*()> selectionProvider;

    te::Clip* currentClip = nullptr;   // identity for change detection
    te::Clip::Ptr keepAlive;           // TE clips are refcounted — this keeps the shown
                                       // clip OBJECT alive across the poll gap even if it
                                       // is deleted in the arrangement (no UAF window)

    std::unique_ptr<AudioClipEditor> audioEditor;
    std::unique_ptr<PianoRollView>   pianoRoll;
    juce::Label emptyLabel;

    void rebuildFor (te::Clip* clip);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DockedEditorPanel)
};
