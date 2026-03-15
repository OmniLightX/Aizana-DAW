#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "app/ApplicationState.h"
#include "engine/ClipEditor.h"
#include "engine/MixerEngine.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "TrackHeader.h"
#include "ClipComponent.h"

namespace te = tracktion::engine;

/**
 * ArrangementView — Linear timeline with track rows, track headers, and clip blocks.
 *
 * Layout:
 *   - Left panel (fixed ~160px): vertical list of TrackHeaders
 *   - Right panel: scrollable timeline with ClipComponents
 *
 * Reactive to Edit's ValueTree: rebuilds track rows and clips when tracks are
 * added/removed or clips change. This is the ValueTree-as-source-of-truth pattern
 * per RESEARCH.md Pattern 1.
 *
 * Track row height: 60px (fixed in Phase 1; variable-height rows in Phase 2).
 *
 * Plan 01-15: TrackHeaders now receive PluginManager, PluginWindowManager, and
 * PluginScanner so that right-click context menus can insert plugins.
 */
class ArrangementView : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ValueTree::Listener,
                        private juce::ScrollBar::Listener
{
public:
    ArrangementView (ApplicationState&    appState,
                     ClipEditor&          clipEditor,
                     MixerEngine&         mixerEngine,
                     PluginManager&       pluginManager,
                     PluginWindowManager& pluginWindowManager,
                     PluginScanner&       pluginScanner);
    ~ArrangementView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Zoom control (pinch/scroll — also called by MainComponent)
    double getPixelsPerSecond() const noexcept { return pixelsPerSecond; }
    void   setPixelsPerSecond (double pps);

    // Add Track button accessor — used by MainComponent for Cmd+T shortcut
    juce::TextButton& getAddTrackButton() noexcept { return addTrackButton; }

    // FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files) override;

private:
    ApplicationState&    appState;
    ClipEditor&          clipEditor;
    MixerEngine&         mixerEngine;
    PluginManager&       pluginManager;
    PluginWindowManager& pluginWindowManager;
    PluginScanner&       pluginScanner;

    // ---- Header strip ----
    juce::TextButton addTrackButton { "+ Track" };
    bool isDragHovering = false;

    // ---- Left panel: track headers ----
    juce::Viewport headerViewport;
    juce::Component headerContainer;   // sized to fit all track headers vertically

    // ---- Right panel: timeline ----
    juce::Viewport timelineViewport;
    juce::Component timelineContainer; // sized to fit all clips

    juce::OwnedArray<TrackHeader>   trackHeaders;
    juce::OwnedArray<ClipComponent> clipComponents;

    static constexpr int kTrackHeight       = 60;
    static constexpr int kHeaderWidth       = 160;
    static constexpr int kHeaderStripHeight = 28;   // top strip holding Add Track button

    double pixelsPerSecond    = 100.0;
    double scrollPositionSecs =   0.0;

    // Rebuilds all header + clip children from current Edit state
    void rebuildTrackRows();

    // ValueTree::Listener — called when tracks are added/removed/reordered
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int)   override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeParentChanged   (juce::ValueTree&) override {}

    // ScrollBar::Listener — keeps header/timeline vertical scroll in sync
    void scrollBarMoved (juce::ScrollBar* scrollBar, double newRangeStart) override;

    // Keep header and timeline scroll positions in sync
    void syncScrollPositions();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
