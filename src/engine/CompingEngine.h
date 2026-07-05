#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "EditManager.h"

namespace te = tracktion::engine;

/**
 * CompingEngine — project-less comping for WaveAudioClip loop takes.
 *
 * Why not te::WaveCompManager? TE's comp manager resolves take sources through
 * ProjectManager::findSourceFile (ProjectItemID) — but Aizana records into
 * project-less edits, where loop takes are stored as DIRECT file references
 * (WaveAudioClip::addTake (const File&)). TE's comp render would silently
 * produce silence, and WaveAudioClip::setCurrentTake() even REMOVES a take
 * when its ProjectItemID can't be resolved. So Aizana keeps its own comp
 * model and renderer that work purely on file paths.
 *
 * Model (persisted in the clip's ValueTree, all mutations undoable):
 *   <AIZANACOMP>                      child of the clip state
 *     <SECTION takeIndex="0" endTime="3.5"/>   sections sorted by endTime;
 *     <SECTION takeIndex="2" endTime="8.0"/>   a section starts where the
 *   </AIZANACOMP>                             previous one ends (first at 0)
 *
 * Comp time domain: seconds 0..getCompLength() — identical to take-local time
 * (every loop take file is one pass starting at the loop start).
 *
 * Render: debounced (300 ms) on a background ThreadPool; mixes the take
 * segments with 10 ms equal-power crossfades into a cached WAV, then applies
 * it on the message thread via Clip::setCurrentSourceFile() — TE's own
 * transient comp-override mechanism (playback follows, persisted source stays
 * the original take). Undo/redo re-triggers the render through the ValueTree
 * listener, and attachToEdit() re-renders active comps after session load.
 */
class CompingEngine : private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    explicit CompingEngine (EditManager& em);
    ~CompingEngine() override;

    static const juce::Identifier compType;       // "AIZANACOMP"
    static const juce::Identifier sectionType;    // "SECTION"
    static const juce::Identifier takeIndexProp;  // "takeIndex"
    static const juce::Identifier endTimeProp;    // "endTime"

    struct Section
    {
        int    takeIndex = -1;   // -1 = silence
        double endTime   = 0.0;  // seconds, comp domain
    };

    /** (Re)attach the ValueTree listener to the current edit and re-render all
        active comps. Call after EditManager creates or loads an edit. */
    void attachToEdit();

    //==========================================================================
    // Queries (message thread)

    bool hasComp (const te::WaveAudioClip&) const;
    juce::Array<Section> getSections (const te::WaveAudioClip&) const;

    /** Resolved take source files, in take order (empty File when unresolvable). */
    juce::Array<juce::File> getTakeFiles (const te::WaveAudioClip&) const;

    /** Comp length in seconds = longest take file duration (0 when no takes). */
    double getCompLength (const te::WaveAudioClip&) const;

    /** Robust active-take lookup that works for direct-file takes: compares
        source strings first, then the RESOLVED files (take sources are stored
        relative, the clip's own source may be absolute). Static so ClipEditor
        can share the logic. Returns -1 when no take matches. */
    static int activeTakeIndexFor (const te::WaveAudioClip&);

    int getActiveTakeIndex (const te::WaveAudioClip& c) const { return activeTakeIndexFor (c); }

    //==========================================================================
    // Gestures (message thread, each = one undo transaction)

    /** Use takeIndex for the comp range [t0, t1] (the Studio One lane drag). */
    void applyTakeSection (te::WaveAudioClip&, double t0, double t1, int takeIndex);

    /** Switch the whole section containing `time` to takeIndex (lane click). */
    void switchSectionAt (te::WaveAudioClip&, double time, int takeIndex);

    /** Remove the comp — the clip falls back to its persisted take source. */
    void clearComp (te::WaveAudioClip&);

    /** Select a whole take as the clip source (replacement for TE's broken
        setCurrentTake on direct-file takes); also removes any active comp. */
    void selectWholeTake (te::WaveAudioClip&, int takeIndex);

    //==========================================================================
    /** Synchronous render + apply, used by tests (UI path is debounced/async).
        Returns the rendered file (empty on failure). */
    juce::File renderCompNow (te::WaveAudioClip&);

    /** Transient playback-source override = te::Clip::setCurrentSourceFile.
        That member is protected and TE only friends its own WaveCompManager;
        the .cpp lifts it via the legal explicit-instantiation exemption so we
        don't have to patch the vendored TE submodule. Empty file = back to the
        clip's persisted source. */
    static void setSourceOverride (te::Clip&, const juce::File&);

private:
    EditManager& editManager;
    juce::ValueTree attachedRoot;
    juce::Array<te::EditItemID> pendingClips;
    juce::ThreadPool renderPool { 1 };
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    juce::ValueTree getCompTree (const te::WaveAudioClip&) const;
    juce::ValueTree getOrCreateCompTree (te::WaveAudioClip&, juce::UndoManager&);
    juce::Array<Section> readSections (const juce::ValueTree& compTree) const;
    void writeSections (juce::ValueTree& compTree, const juce::Array<Section>&, juce::UndoManager&);

    void scheduleRenderForClipState (juce::ValueTree clipState);
    void startRenderJob (te::WaveAudioClip&);
    void applyRenderedFile (te::EditItemID, const juce::File&);

    /** Pure render core — background-thread-safe, only touches copied data. */
    static juce::File renderCompFile (const juce::Array<juce::File>& takeFiles,
                                      const juce::Array<Section>& sections,
                                      const juce::File& destFile);

    static juce::File compCacheDirectory();
    static juce::String compHash (const te::EditItemID&,
                                  const juce::Array<juce::File>&,
                                  const juce::Array<Section>&);

    // juce::Timer — debounce
    void timerCallback() override;

    // juce::ValueTree::Listener
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleTreeChange (juce::ValueTree);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompingEngine)
};
