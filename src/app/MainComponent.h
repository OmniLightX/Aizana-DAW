#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ApplicationState.h"
#include "engine/MixerEngine.h"
#include "engine/InputBusManager.h"
#include "engine/AutomationEngine.h"
#include "engine/ClipEditor.h"
#include "engine/ExportPipeline.h"
#include "ai/StemImportEngine.h"
#include "ai/MixApplyEngine.h"
#include "ai/MixDataLogger.h"
#include "ai/StemSeparationEngine.h"
#include "ai/ReferenceAnalyzer.h"
#include "ui/stems/StemImportDialog.h"
#include "ui/stems/SeparationProgressPanel.h"
#include "ui/mix/MixPreviewDialog.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginScanner.h"
#include "ui/arrangement/TransportBar.h"
#include "ui/arrangement/ArrangementView.h"
#include "ui/mixer/MixerView.h"
#include "ui/pluginwin/PluginWindowManager.h"
#include "ui/preferences/PreferencesDialog.h"
#include "ui/prefs/AudioPreferencesDialog.h"
#include "ui/export/ExportDialog.h"
#include "ui/audiosetup/AudioIOSetupDialog.h"
#include "ui/pianoroll/PianoRollView.h"
#include "ui/comping/CompingView.h"
#include "ui/inspector/InspectorPanel.h"
#include "ui/beginner/BeginnerView.h"
#include "ui/onboarding/OnboardingWizard.h"
#include "ui/pluginbrowser/ScanProgressPanel.h"
#include "ui/assist/ProAssistPalette.h"
#include "ui/assist/ProAssistRail.h"

/**
 * MainComponent — Root JUCE Component for the Cloudes DAW window.
 *
 * Four-region layout (CONTEXT.md + plan 01-10):
 *   1. TransportBar      — fixed 50px at top
 *   2. ArrangementView   — timeline, fills remaining upper area
 *   3. MixerView         — horizontal channel strips at bottom (~35% of remaining height)
 *   4. DockedPluginPanel — collapsible right-side panel (hidden when empty)
 *
 * MixerView is visible by default (mixing-first per CONTEXT.md).
 * Cmd+M (macOS) / Ctrl+M (Windows) toggles MixerView visibility.
 *
 * DockedPluginPanel width is 350px when plugins are docked, 0px when empty.
 *
 * Engine objects (MixerEngine, AutomationEngine, ClipEditor) are owned here
 * and shared by reference with child components that need them.
 *
 * Plan 01-13: MainComponent now implements juce::MenuBarModel, providing
 * File, Edit, View, and Export menus. MainWindow wires this to the native
 * macOS system menu bar (setMacMainMenu) or an in-window MenuBarComponent
 * on Windows/Linux.
 *
 * Plan 01-15: MainComponent owns PluginScanner and PluginManager. Both are
 * passed down to ArrangementView → TrackHeader for the right-click plugin
 * insert workflow.
 */
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::ChangeListener
{
public:
    explicit MainComponent (ApplicationState& appState);
    ~MainComponent() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** Access the PluginWindowManager for opening plugin editor windows. */
    PluginWindowManager& getPluginWindowManager() noexcept { return pluginWindowManager; }

    /** Show the docked Piano Roll for the given MIDI clip. Called by ClipComponent::mouseDoubleClick. */
    void showPianoRoll (te::MidiClip& midiClip);
    /** Hide the docked Piano Roll panel. */
    void hidePianoRoll();

    /** Show the docked Comping editor for a WaveAudioClip with loop takes.
        Shares the bottom dock slot with the Piano Roll (mutually exclusive). */
    void showCompingEditor (te::WaveAudioClip& clip);
    /** Hide the docked Comping editor. */
    void hideCompingEditor();

    // -------------------------------------------------------------------------
    // juce::MenuBarModel overrides (pure virtual — all three must be implemented)
    // -------------------------------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex,
                                       const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    ApplicationState& appState;

    // Engine objects — owned by MainComponent, shared with UI components.
    // Declaration order matters: objects are initialised in declaration order.
    // PluginScanner has no dependencies on other members.
    // PluginManager depends on PluginScanner + EditManager (via appState).
    MixerEngine      mixerEngine      { appState.editManager };
    InputBusManager  inputBusManager  { appState.editManager };
    AutomationEngine automationEngine { appState.editManager };
    ClipEditor       clipEditor       { appState.editManager };
    CompingEngine    compingEngine    { appState.editManager };

    // Export pipeline — owned by MainComponent, passed to ExportDialog (plan 01-16)
    // ExportPipeline() takes no constructor args; edit is passed per-call
    ExportPipeline   exportPipeline;

    // KI-Setup (Phase 3, Iteration 1): SessionPlan → session via AICommandBridge.
    // Depends on mixerEngine (declared above — init order OK).
    StemImportEngine stemImportEngine { appState.editManager, mixerEngine,
                                        appState.aiCommandBridge };

    // KI-Mixing (Phase 4, Iteration 1): MixPlan → session via AICommandBridge.
    MixApplyEngine   mixApplyEngine   { appState.editManager, mixerEngine, appState.aiCommandBridge };

    // Stem-Separation (Phase 3, Spike S1 GO): full song → stems on a background
    // thread (vendored demucs.cpp). Feeds the same KI-Setup pipeline afterwards.
    StemSeparationEngine stemSeparationEngine;

    // Plugin scanning + management (plan 01-15)
    // PluginScanner must be declared before PluginManager (dependency order)
    PluginScanner    pluginScanner;
    PluginManager    pluginManager    { pluginScanner, appState.editManager };

    // Plugin window management (plan 01-10)
    PluginWindowManager pluginWindowManager { *this };

    // UI components
    TransportBar    transportBar    { appState };
    ArrangementView arrangementView { appState, clipEditor, mixerEngine,
                                      pluginManager, pluginWindowManager, pluginScanner,
                                      automationEngine, inputBusManager };
    MixerView       mixerView       { appState, mixerEngine,
                                      pluginManager, pluginWindowManager, pluginScanner };
    // dockedPanel is inside pluginWindowManager — added to our layout via getDockedPanel()

    // Beginner world (Phase 2, Block A) — a second view over the SAME te::Edit.
    // Visibility is driven by the Dial (appState.uiState); see applyUIMode().
    // Needs MixerEngine to read Named-Bus grouping (declared earlier — init order OK).
    BeginnerView    beginnerView    { appState, mixerEngine };

    // Pro-Assist dockable rail (Sketch 004, A of B+A) — opt-in right panel, shown in
    // Pro mode when assist is toggled on (TransportBar "Assist" → uiState.assistEnabled).
    ProAssistRail   proAssistRail   { appState };

    // Inspector — left dock with selection parameters (Studio One pattern).
    // Hidden by default; View menu / Cmd+I toggles.
    InspectorPanel  inspectorPanel  { appState.editManager, mixerEngine };
    bool inspectorVisible = false;
    void toggleInspector();

    bool mixerVisible = true;   // CONTEXT.md: mixer visible by default

    // ---- Mixer divider — click to toggle, drag to resize (Studio One style) ----
    // Replaces the former juce::TextButton mixerToggleButton.
    // The component handles both a click (< 5px movement = toggle) and a
    // vertical drag (adjusts mixerHeightPx).
    class MixerDivider : public juce::Component
    {
    public:
        MixerDivider() = default;

        std::function<void()>    onToggleClick;   // fired on short click
        std::function<void(int)> onDragDelta;     // fired during drag: delta Y in screen pixels
        bool mixerExpanded = true;                // controls arrow direction in paint()

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1a1a1a));
            // Subtle top/bottom border lines
            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine (0, 0.0f, (float)getWidth());
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());
            // Arrow + label
            const juce::juce_wchar arrow = mixerExpanded ? 0x25BC : 0x25B2;
            const juce::String text = juce::String::charToString (arrow) + "  Mixer";
            g.setColour (juce::Colour (0xff888888));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragStartScreenY = e.getScreenY();
            dragDeltaAccum   = 0;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (onDragDelta == nullptr) return;
            const int total = e.getScreenY() - dragStartScreenY;
            const int step  = total - dragDeltaAccum;
            if (step != 0)
            {
                onDragDelta (step);
                dragDeltaAccum = total;
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            // Treat as click only when the mouse barely moved (< 5px)
            if (e.getDistanceFromDragStart() < 5 && onToggleClick)
                onToggleClick();
        }

        juce::MouseCursor getMouseCursor() override
        {
            return juce::MouseCursor::UpDownResizeCursor;
        }

    private:
        int dragStartScreenY = 0;
        int dragDeltaAccum   = 0;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerDivider)
    };

    MixerDivider      mixerDivider;
    int               mixerHeightPx       = 0;   // 0 = unset; initialised on first resized()
    static constexpr int kMixerToggleBarHeight  = 18;
    static constexpr int kMixerMinHeight        = 120; // minimum mixer panel height (px)
    static constexpr int kArrangementMinHeight  = 60;  // minimum arrangement panel height (px)

    void updateMixerDivider();

    bool pianoRollVisible = false;
    std::unique_ptr<PianoRollView> pianoRollView;  // current docked Piano Roll (nullptr when hidden)
    juce::TextButton pianoRollCloseButton { "X" };  // close button in piano roll header
    static constexpr int kPianoRollHeight = 250;  // default docked height

    // Comping editor — shares the bottom dock slot with the Piano Roll
    bool compingVisible = false;
    std::unique_ptr<CompingView> compingView;       // nullptr when hidden
    juce::TextButton compingCloseButton { "X" };

    static constexpr int kDockedPanelWidth = 350;  // px when plugins are docked
    static constexpr int kAssistRailWidth  = 330;  // px for the Pro-Assist rail (Sketch 004)

    void toggleMixer();

    // Edit/Mix view switcher (TransportBar segmented pair): Edit = arrangement
    // full height, Mix = mixer shown large. Cmd+M / divider keep working.
    void setViewFocus (bool mixFocus);

    // Beginner world (Phase 2, Block A): the Dial swaps which view is visible.
    // applyUIMode() shows/hides the Pro composition vs the BeginnerView and re-lays out.
    void applyUIMode();
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // First-run onboarding (Phase 2, Block C) — shown once, gated by UIStateManager.
    void showOnboardingIfNeeded();
    // Build + show the wizard unconditionally (the actual overlay creation).
    void showOnboardingWizard();
    // Menu action: re-run onboarding on demand ("Onboarding wiederholen") so the genre
    // presets + starter-tempo slider are reachable again after first run.
    void handleReopenOnboarding();
    std::unique_ptr<OnboardingWizard> onboardingWizard;

    // -------------------------------------------------------------------------
    // Menu item IDs — enum avoids magic numbers and ensures uniqueness
    // -------------------------------------------------------------------------
    enum MenuItemIDs
    {
        // File menu
        FileNewSession  = 1,
        FileOpen        = 2,
        FileSave        = 3,
        FileSaveAs      = 4,
        FileImportAudio = 5,
        FileImportStems = 6,

        // Edit menu
        EditUndo = 101,
        EditRedo = 102,

        // View menu
        ViewToggleMixer     = 201,
        ScanPlugins         = 202,
        ViewToggleBeginner  = 203,
        ViewReopenOnboarding = 204,
        ViewToggleInspector = 205,

        // Export menu
        ExportMixdown = 301,
        ExportStems   = 302,

        // Song Setup menu
        SongSetupAudioIO  = 401,
        SongSeparateStems = 402,
        SongAddMarker     = 403,
        SongMixAI         = 404,   // KI-Mix (Phase 4 — heuristic mixing co-pilot)
        SongRefGenreBase  = 410,   // 410..413 = genre profiles (REF-02)
        SongRefAnalyze    = 420,   // analyse reference track (REF-01)
        SongRefClear      = 421,

        // Cloudes menu
        CloudesPreferences = 501,
    };

    // Double-tap Space = Rewind: tracks timestamp of the previous Space press.
    juce::int64 lastSpaceKeyTimeMs = 0;
    static constexpr juce::int64 kDoubleSpaceThresholdMs = 400;

    // Async file chooser — stored as member to keep alive across async callbacks
    std::unique_ptr<juce::FileChooser> fileChooser;

    // -------------------------------------------------------------------------
    // File menu handlers
    // -------------------------------------------------------------------------
    void handleFileNew();
    void handleFileOpen();
    void handleFileSave();
    void handleFileSaveAs();
    void handleFileImportAudio();

    // KI-Setup (Phase 3, Slice 4): analyse stems on a worker thread, then apply
    // mode-dependently — Beginner instant (one undo), Pro via preview dialog (E3).
    void handleFileImportStems();
    void startStemAnalysis (const juce::Array<juce::File>& files);
    void onStemPlanReady (const SessionPlan& plan);

    // KI-Mix (Phase 4, Slice 4): scan tracks (message thread) → measure + mix on a
    // worker → apply mode-dependently (Beginner instant / Pro preview dialog).
    void startMix();
    void onMixPlanReady (const cloudes::MixPlan& plan,
                         const std::vector<cloudes::TrackMixProfile>& profiles);

    // KI-Referenz (Phase 3, Iteration 2 — REF-01/02/04): genre profile or
    // analysed reference track, persisted in the Edit state. Guides gain
    // staging; LUFS is reference info only, never enforced.
    ReferenceProfile getActiveReferenceProfile() const;
    void setActiveReferenceProfile (const ReferenceProfile&);
    void clearActiveReferenceProfile();
    void handleAnalyzeReferenceTrack();

    // Stem-Separation (Phase 3): song → 6 stems → KI-Setup pipeline.
    void handleSongSeparateStems();
    void startStemSeparationFlow (const juce::File& song);
    void downloadSeparationModel (const juce::File& songToContinueWith);
    void pollSeparationModelDownload (const juce::File& songToContinueWith);
    void runStemSeparation (const juce::File& song, StemSeparationEngine::Model);
    void onStemsSeparated (const juce::Array<StemSeparationEngine::Stem>& stems);
    void showSeparationProgress();
    void hideSeparationProgress();

    std::unique_ptr<SeparationProgressPanel>    separationPanel;
    std::unique_ptr<juce::URL::DownloadTask>    modelDownloadTask;

    // -------------------------------------------------------------------------
    // View menu handlers
    // -------------------------------------------------------------------------
    // userInitiated=true shows a "Scan Complete" alert at the end (menu action);
    // auto-scans/resumes at startup pass false and finish silently.
    void handleScanPlugins (bool userInitiated = true);

    // Modeless scan-progress overlay (created on demand; nullptr when idle).
    void showScanProgress();
    void hideScanProgress();

    // Copy every scanned plugin description into the Tracktion Engine's knownPluginList,
    // so TE can instantiate third-party plugins (editor + audio). Cloudes scans into its
    // own list; TE resolves instances from the engine's list. Call after load and scan.
    void syncScannedPluginsToEngine();

    // Returns the path to the cached plugin list XML file
    // (~/<AppData>/Cloudes/knownPlugins.xml on all platforms)
    static juce::File getPluginListCacheFile();

    // Modeless scan-progress overlay — owned here, present only while scanning.
    std::unique_ptr<ScanProgressPanel> scanProgressPanel;

    // Pro-Assist command palette (⌘K, Sketch 004) — summoned in Pro mode, dismisses itself.
    std::unique_ptr<ProAssistPalette> proAssistPalette;
    void showProAssistPalette();
    void applyAssistIntent (IntentEngine::Intent);   // shared by palette + (later) rail

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
