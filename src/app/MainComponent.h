#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/instruments/MusicalTypingWindow.h"
#include "ApplicationState.h"
#include "UndoMenuObserver.h"
#include "engine/MixerEngine.h"
#include "engine/InputBusManager.h"
#include "engine/AutomationEngine.h"
#include "engine/ClipEditor.h"
#include "engine/ExportPipeline.h"
#include "ai/StemImportEngine.h"
#include "ai/MixApplyEngine.h"
#include "ai/AiStudioLiveServer.h"
#include "audio/DropoutMonitor.h"
#include "ai/MixDataLogger.h"
#include "ai/StemSeparationEngine.h"
#include "generator/PluginGenerator.h"
#include "plugins/ClapPluginFormat.h"
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
#include "ui/dockeditor/DockedEditorPanel.h"
#include "ui/inspector/InspectorPanel.h"
#include "ui/beginner/BeginnerView.h"
#include "ui/onboarding/OnboardingWizard.h"
#include "ui/pluginbrowser/ScanProgressPanel.h"
#include "ui/assist/ProAssistPalette.h"
#include "ui/assist/ProAssistRail.h"

/**
 * MainComponent — Root JUCE Component for the Aizana DAW window.
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

    /**
     * Oeffnet eine Session-Datei — derselbe Weg wie "Open Session…", nur ohne den
     * Dateidialog. Wird auch beim Programmstart mit einer Datei als Argument und
     * beim Doppelklick im Finder benutzt.
     *
     * Rueckgabe: false, wenn die Datei nicht als Session lesbar war (die laufende
     * Session bleibt dann unangetastet).
     */
    bool openSessionFile (const juce::File& file);

    /** Beim Start: fragen, wenn die letzte Sitzung nicht sauber beendet wurde. */
    void offerCrashRecovery();

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
    UndoMenuObserver undoMenuObserver { [this] { menuItemsChanged(); } };

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

    // KI-Studio (Phase 4, flagship, Slice 2): hosts the loopback live bridge so the in-app
    // agent's aizana-mcp can read tracks + change the mix on the RUNNING session. Started in
    // the constructor, stopped on destruction. Mutations route through mixApplyEngine (1 undo).
    aizana::AiStudioLiveServer aiLiveServer { appState.editManager, mixApplyEngine, appState.aiCommandBridge };

    // Stem-Separation (Phase 3, Spike S1 GO): full song → stems on a background
    // thread (native ONNX/Mega 53). Feeds the same KI-Setup pipeline afterwards.
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

    // Docked Editor (Studio One "Edit" view) — bottom dock, driven by the
    // GLOBAL arrangement selection (no separate editor selection/data).
    DockedEditorPanel dockedEditorPanel { appState.editManager,
        [this]() -> te::Clip* { return arrangementView.getPrimarySelectedClip(); },
        &compingEngine };

    bool mixerVisible = true;   // derived: activeBottomDock == Mix (kept for menu/beginner paths)

    // ---- Bottom dock (Studio One "Edit | Mix | Browse") -------------------
    // ONE bottom slot shared by the Docked Editor ("Edit") and the Mixer
    // ("Mix"); clicking the active button closes the slot. "Browse" opens the
    // plugin browser. State + height are persisted across launches.
    enum class BottomDock { None, Edit, Mix };
    BottomDock activeBottomDock = BottomDock::Mix;   // mixing-first default

    void openBottomDock (BottomDock d);              // non-toggling (force open/close)
    void setBottomDock  (BottomDock requested);      // toggle semantics (button clicks)
    void persistBottomDockState();
    void loadBottomDockState();
    void showBrowsePopup();

    // ---- Bottom bar — drag to resize, hosts the Edit/Mix/Browse buttons ----
    class MixerDivider : public juce::Component
    {
    public:
        MixerDivider()
        {
            for (auto* b : { &editButton, &mixButton, &browseButton })
            {
                b->setClickingTogglesState (false);
                b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff262626));
                b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2266aa));
                b->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffbbbbbb));
                b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
                addAndMakeVisible (*b);
            }
            editButton.onClick   = [this] { if (onEditClick)   onEditClick(); };
            mixButton.onClick    = [this] { if (onMixClick)    onMixClick(); };
            browseButton.onClick = [this] { if (onBrowseClick) onBrowseClick(); };

            // Mehrdeutige Namen qualifizieren (Rest der a11y-Welle f846eb9): "Edit" und
            // "Mix" gibt es ZWEIMAL — oben in der TransportBar als Ansichtsumschalter,
            // hier unten als Dock-Waehler. Wer nur "Edit" anspricht, trifft mal das eine,
            // mal das andere. Gezeichnet wird der Titel nicht, die Beschriftung bleibt.
            editButton  .setTitle (juce::String::fromUTF8 ("Unten Editor"));
            mixButton   .setTitle (juce::String::fromUTF8 ("Unten Mixer"));
            browseButton.setTitle (juce::String::fromUTF8 ("Unten Browser"));
        }

        std::function<void()>    onEditClick, onMixClick, onBrowseClick;
        std::function<void(int)> onDragDelta;     // fired during drag: delta Y in screen pixels
        std::function<void()>    onDragEnd;       // persist height after a resize drag
        std::function<void()>    onToggleClick;   // short click on empty bar area
        bool mixerExpanded = true;                // any dock open? (arrow direction)

        /** Highlight the active dock button (0 = none, 1 = edit, 2 = mix). */
        void setActiveDock (int which)
        {
            editButton.setToggleState (which == 1, juce::dontSendNotification);
            mixButton .setToggleState (which == 2, juce::dontSendNotification);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            browseButton.setBounds (r.removeFromRight (64));
            r.removeFromRight (4);
            mixButton.setBounds  (r.removeFromRight (54));
            r.removeFromRight (2);
            editButton.setBounds (r.removeFromRight (54));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1a1a1a));
            g.setColour (juce::Colour (0xff444444));
            g.drawHorizontalLine (0, 0.0f, (float)getWidth());
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float)getWidth());
            const juce::juce_wchar arrow = mixerExpanded ? 0x25BC : 0x25B2;
            g.setColour (juce::Colour (0xff888888));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String::charToString (arrow),
                        getLocalBounds().withTrimmedLeft (8),
                        juce::Justification::centredLeft, false);
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
            if (e.getDistanceFromDragStart() < 5)
            {
                if (onToggleClick)
                    onToggleClick();
            }
            else if (onDragEnd)
                onDragEnd();
        }

        juce::MouseCursor getMouseCursor() override
        {
            return juce::MouseCursor::UpDownResizeCursor;
        }

    private:
        juce::TextButton editButton   { "Edit" };
        juce::TextButton mixButton    { "Mix" };
        juce::TextButton browseButton { "Browse" };
        int dragStartScreenY = 0;
        int dragDeltaAccum   = 0;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerDivider)
    };

    MixerDivider      mixerDivider;
    int               mixerHeightPx       = 0;   // bottom dock height; 0 = unset (default on first resized())
    static constexpr int kMixerToggleBarHeight  = 24;
    static constexpr int kMixerMinHeight        = 120; // minimum bottom dock height (px)
    static constexpr int kArrangementMinHeight  = 60;  // minimum arrangement panel height (px)

    void updateMixerDivider();

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
        SongGeneratePlugin = 405,  // Plugin generieren (Phase 4 — Faust→CLAP generator)
        SongAiStudio      = 406,   // KI-Studio: in-app chat agent (Claude/Codex) — Phase 4 flagship
        SongManagePlugins = 407,   // Aizana-Plugins verwalten (Phase 5 / G5b — rename/delete)
        SongMusicalTyping = 408,   // Tastatur spielen (Logic "Musical Typing"-Aequivalent)
        SongRefGenreBase  = 410,   // 410..413 = genre profiles (REF-02)
        SongRefAnalyze    = 420,   // analyse reference track (REF-01)
        SongRefClear      = 421,

        // Aizana menu
        AizanaPreferences = 501,
    };

    // Double-tap Space = Rewind: tracks timestamp of the previous Space press.
    juce::int64 lastSpaceKeyTimeMs = 0;
    static constexpr juce::int64 kDoubleSpaceThresholdMs = 400;

    /** Fenster "Tastatur spielen" — Instrumente ohne angeschlossenes Keyboard
        spielen. Existiert nur, solange es offen ist. */
    std::unique_ptr<MusicalTypingWindow> musicalTypingWindow;
    void toggleMusicalTyping();

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
    void onMixPlanReady (const aizana::MixPlan& plan,
                         const std::vector<aizana::TrackMixProfile>& profiles);

    // Plugin generator — RUNTIME path (Phase 4 / Generator G3): describe a plugin → the
    // local composer writes Faust → FaustCompiler builds a brand-new .clap on a background
    // thread → load it onto the selected track + open its editor. This is the real generator
    // (writes new DSP), superseding the precompiled-palette path below.
    void startRuntimeGenerator();
    void runGeneratorFromPrompt (const juce::String& prompt, te::EditItemID trackID);
    void onGeneratedPluginReady (bool ok, const juce::String& clapPath, const juce::String& log,
                                 const juce::String& pluginName, te::EditItemID trackID);
    bool        generatorBusy = false;            // guards against concurrent generations
    juce::Label generatorStatusLabel;             // transient "Generiere…" overlay

    // Plugin generator — PALETTE path (Phase 4 / Iteration B, B2): pick/describe a bundled
    // precompiled template. Dormant (superseded by the runtime path); kept until B-rollback.
    void startPluginGenerator();
    void describeAndGenerate();   // free-text prompt → match → generate on the selected track
    void generatePluginOnTrack (const aizana::GeneratorTemplate& tmpl, te::AudioTrack& track);

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
    void pollSeparationModelDownload (const juce::File& songToContinueWith, uint64_t generation);
    void cancelStemSeparationWork();
    void runStemSeparation (const juce::File& song, StemSeparationEngine::Model);
    void onStemsSeparated (const juce::Array<StemSeparationEngine::Stem>& stems);
    void showSeparationProgress();
    void hideSeparationProgress();

    std::unique_ptr<SeparationProgressPanel>    separationPanel;
    std::unique_ptr<juce::URL::DownloadTask>    modelDownloadTask;
    juce::File separationModelTemporary;
    bool separationModelPreparing = false;
    uint64_t separationGeneration = 0;

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
    // so TE can instantiate third-party plugins (editor + audio). Aizana scans into its
    // own list; TE resolves instances from the engine's list. Call after load and scan.
    void syncScannedPluginsToEngine();

    // Picks up plugins authored by an external MCP client (Claude/Codex via aizana-mcp)
    // while Aizana is running, so they appear in the browser without a restart. Cheap:
    // only re-registers (dlopen) when the .clap count in the generated dir actually changes.
    void pollGeneratedLibrary();

    // Lightweight 4s message-thread timer driving pollGeneratedLibrary().
    struct GeneratedLibraryWatcher : public juce::Timer
    {
        std::function<void()> onTick;
        void timerCallback() override { if (onTick) onTick(); }
    };
    GeneratedLibraryWatcher generatedLibraryWatcher;
    int lastGeneratedClapCount = -1;

    // M13-Diagnose: protokolliert XRuns (hörbare Aussetzer) mit App-Kontext nach
    // ~/Library/Aizana/logs/dropouts.log — bis die Dropout-Ursache bestätigt ist.
    std::unique_ptr<DropoutMonitor> dropoutMonitor;

    // KI-Studio (Phase 4 flagship): in-app chat that drives the user's Claude/Codex CLI.
    void openAiStudio();
    std::unique_ptr<juce::DocumentWindow> aiStudioWindow;

    void openGeneratedPluginsManager();   // Phase 5 / G5b
    std::unique_ptr<juce::DocumentWindow> generatedPluginsWindow;

    // Returns the path to the cached plugin list XML file
    // (~/<AppData>/Aizana/knownPlugins.xml on all platforms)
    static juce::File getPluginListCacheFile();

    // Modeless scan-progress overlay — owned here, present only while scanning.
    std::unique_ptr<ScanProgressPanel> scanProgressPanel;

    // Pro-Assist command palette (⌘K, Sketch 004) — summoned in Pro mode, dismisses itself.
    std::unique_ptr<ProAssistPalette> proAssistPalette;
    void showProAssistPalette();
    void applyAssistIntent (IntentEngine::Intent);   // shared by palette + (later) rail

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
