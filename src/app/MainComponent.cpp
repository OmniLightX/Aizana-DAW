#include "MainComponent.h"

namespace te = tracktion::engine;

//==============================================================================

MainComponent::MainComponent (ApplicationState& appState_)
    : appState (appState_)
{
    // ── Named Input Buses (Phase 2) ──────────────────────────────────────────
    // Re-seed / re-bind Named Buses whenever the Edit is swapped (File>New /
    // File>Open). A fresh or loaded Edit does not inherit the previous Edit's
    // <INPUTBUSES> tree, so seed defaults when none exist and push every bus
    // name onto its WaveInputDevice alias (the hybrid binding step).
    appState.editManager.onEditChanged = [this]
    {
        inputBusManager.initialiseDefaultBuses();
        inputBusManager.applyToEngine();
    };

    // The startup Edit was created by ApplicationState before this component (and
    // its InputBusManager) existed, so onEditChanged could not fire for it — seed now.
    inputBusManager.initialiseDefaultBuses();
    inputBusManager.applyToEngine();

    addAndMakeVisible (transportBar);
    addAndMakeVisible (arrangementView);
    addAndMakeVisible (mixerView);

    // DockedPluginPanel from PluginWindowManager — right-side panel
    addAndMakeVisible (pluginWindowManager.getDockedPanel());

    // Mixer visible by default (CONTEXT.md: mixing-first design)
    mixerView.setVisible (mixerVisible);

    // Crash-isolate plugin scanning: a broken plugin blacklists only itself instead of
    // taking down the whole scan/app (Fix 4). Must precede any scanForPlugins() call.
    pluginScanner.enableCrashIsolatedScanning();

    // Cache file for incremental, crash-safe persistence DURING a scan. With this set,
    // the scanner writes the partial list periodically + at every format boundary, so a
    // large library (1000+ plugins) survives an interrupted scan and resumes next launch.
    const auto pluginCache = getPluginListCacheFile();
    pluginScanner.setPluginListCacheFile (pluginCache);

    // Load previously-scanned plugin list so the browser is populated on startup.
    pluginScanner.loadPluginList (pluginCache);

    // Make the cached plugins instantiable by Tracktion Engine (editor + audio), and
    // crucially BEFORE any saved session is reloaded — TE recreates its plugins from the
    // engine's knownPluginList on load.
    syncScannedPluginsToEngine();

    // First launch (no cache file yet) → scan. Previous scan interrupted (partial cache)
    // → resume; PluginDirectoryScanner skips already-known files, so each launch picks up
    // where the last left off until the list is complete. Both run silently in the
    // background — only a manual menu scan shows a completion alert.
    // NOTE: getNumTypes() is never 0 here (the ctor pre-loads the 4 built-in FX), so the
    // cache FILE's existence — not the type count — is the correct "never scanned" signal.
    const bool neverScanned = ! pluginCache.existsAsFile();
    const bool scanPartial  = ! pluginScanner.lastScanWasComplete();
    if (neverScanned || scanPartial)
    {
        juce::MessageManager::callAsync ([this]
        {
            handleScanPlugins (false);   // auto: silent completion
        });
    }

    // Mixer divider bar — click to toggle, drag to resize (Studio One style)
    mixerDivider.onToggleClick = [this] { toggleMixer(); menuItemsChanged(); };
    mixerDivider.onDragDelta   = [this] (int dy)
    {
        // dy > 0 = dragged down → mixer shrinks; dy < 0 = dragged up → mixer grows
        mixerHeightPx -= dy;
        resized();
    };
    updateMixerDivider();
    addAndMakeVisible (mixerDivider);

    // Beginner world (Phase 2, Block A) — a second view over the SAME te::Edit.
    // Added hidden; the Dial (UIStateManager) decides which view is shown, and
    // switching is lossless because both views share one Edit (UI-04).
    addChildComponent (beginnerView);
    appState.uiState.addChangeListener (this);

    // Allow this component to receive key events
    setWantsKeyboardFocus (true);

    addChildComponent (pianoRollCloseButton);  // hidden by default
    pianoRollCloseButton.onClick = [this] { hidePianoRoll(); };

    // TransportBar must always render on top of all other components (arrangement,
    // mixer, docked panel).  toFront() after all siblings are added guarantees this
    // regardless of add-order — matching Studio One / Logic Pro fixed-transport behaviour.
    transportBar.toFront (false);

    setSize (1280, 800);

    // Reflect the persisted Dial mode into the initial view visibility + layout.
    applyUIMode();

    // First launch: run the onboarding wizard (never an empty session).
    showOnboardingIfNeeded();
}

void MainComponent::showOnboardingIfNeeded()
{
    if (appState.uiState.isOnboardingCompleted())
        return;

    showOnboardingWizard();
}

void MainComponent::showOnboardingWizard()
{
    // Guard against opening a second overlay if one is already up.
    if (onboardingWizard != nullptr)
    {
        onboardingWizard->toFront (true);
        return;
    }

    onboardingWizard = std::make_unique<OnboardingWizard> (appState, [this]
    {
        // Defer teardown out of the wizard's own button callback (avoid self-delete).
        juce::MessageManager::callAsync ([this]
        {
            onboardingWizard.reset();
            applyUIMode();   // land in the (now Beginner) world
        });
    });
    addAndMakeVisible (*onboardingWizard);
    onboardingWizard->setBounds (getLocalBounds());
    onboardingWizard->toFront (true);
}

void MainComponent::handleReopenOnboarding()
{
    // Re-run the first-run wizard on demand. It rebuilds the starter project on the
    // CURRENT edit (renames the first tracks to the genre's names + sets the chosen
    // tempo) in a single undo transaction, so re-running on existing work is reversible.
    // This makes the genre presets + starter-tempo slider reachable after first run.
    showOnboardingWizard();
}

MainComponent::~MainComponent()
{
    // Stop listening to the Dial before teardown (uiState outlives this component).
    appState.uiState.removeChangeListener (this);

    // Detach the Named-Bus edit-change callback before this component (and its
    // InputBusManager member) is destroyed. editManager lives in ApplicationState,
    // which outlives MainComponent — without this, a later createNewEdit()/loadEdit()
    // (e.g. during shutdown) would invoke a lambda capturing a dangling `this`.
    appState.editManager.onEditChanged = nullptr;
}

void MainComponent::resized()
{
    // Onboarding overlay (first run) covers the whole window above everything.
    if (onboardingWizard != nullptr)
        onboardingWizard->setBounds (getLocalBounds());

    // Pro-Assist command palette (⌘K) — full-window overlay (dim + centered card).
    if (proAssistPalette != nullptr)
        proAssistPalette->setBounds (getLocalBounds());

    // Modeless scan-progress overlay — bottom-right corner, above all content.
    // Positioned before the Beginner-mode early-return so it shows in both views.
    if (scanProgressPanel != nullptr)
    {
        constexpr int w = 320, h = 72, margin = 16;
        scanProgressPanel->setBounds (getWidth()  - w - margin,
                                      getHeight() - h - margin, w, h);
        scanProgressPanel->toFront (false);
    }

    // Beginner world owns the whole window when the Dial is in Beginner mode.
    if (appState.uiState.getMode() == UIStateManager::UIMode::Beginner)
    {
        beginnerView.setBounds (getLocalBounds());
        return;
    }

    auto area = getLocalBounds();

    // Fixed transport bar at top (CONTEXT.md: "Fixed transport bar at top")
    transportBar.setBounds (area.removeFromTop (50));

    // DockedPluginPanel — right-side panel (auto-hides when empty)
    auto& dockedPanel = pluginWindowManager.getDockedPanel();
    const int dockedWidth = dockedPanel.isEmpty() ? 0 : kDockedPanelWidth;
    dockedPanel.setBounds (area.removeFromRight (dockedWidth));

    if (pianoRollVisible && pianoRollView != nullptr)
    {
        auto pianoRollArea = area.removeFromBottom (kPianoRollHeight);
        // Close button in top-right corner of piano roll area (20x20)
        pianoRollCloseButton.setBounds (pianoRollArea.getRight() - 24, pianoRollArea.getY() + 4, 20, 20);
        pianoRollView->setBounds (pianoRollArea);
    }

    // Divider bar — always visible; sits between ArrangementView and MixerView
    mixerDivider.setBounds (area.removeFromBottom (kMixerToggleBarHeight));

    // MixerView — height is user-draggable; initialized to 50% of available area on first call
    if (mixerVisible)
    {
        const int totalAvail = area.getHeight();
        if (mixerHeightPx == 0)
            mixerHeightPx = (int)(totalAvail * 0.50f);

        const int maxMixerHeight = juce::jmax (kMixerMinHeight, totalAvail - kArrangementMinHeight);
        mixerHeightPx = juce::jlimit (kMixerMinHeight, maxMixerHeight, mixerHeightPx);
        mixerView.setBounds (area.removeFromBottom (mixerHeightPx));
    }

    // ArrangementView takes remaining upper area
    arrangementView.setBounds (area);

    // Ensure TransportBar stays on top after every layout — adding or showing child
    // components can change z-order and push the transport bar behind the arrangement.
    transportBar.toFront (false);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Cmd+K / Ctrl+K: Pro-Assist command palette (Sketch 004). Pro mode only — the
    // Beginner world already has its own always-on command bar.
    if ((key == juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0) ||
         key == juce::KeyPress ('k', juce::ModifierKeys::ctrlModifier, 0))
        && appState.uiState.getMode() == UIStateManager::UIMode::Pro)
    {
        showProAssistPalette();
        return true;
    }

    // Cmd+, (macOS) / Ctrl+, (Windows) opens Audio Preferences
    if (key == juce::KeyPress (',', juce::ModifierKeys::commandModifier, 0) ||
        key == juce::KeyPress (',', juce::ModifierKeys::ctrlModifier, 0))
    {
        AudioPreferencesDialog::show (appState.audioEngine);
        return true;
    }

    // Cmd+T / Ctrl+T: Add audio track
    if (key == juce::KeyPress ('t', juce::ModifierKeys::commandModifier, 0) ||
        key == juce::KeyPress ('t', juce::ModifierKeys::ctrlModifier, 0))
    {
        arrangementView.getAddTrackButton().triggerClick();
        return true;
    }

    // CONTEXT.md: Cmd+M (macOS) / Ctrl+M (Windows) toggles mixer
    if (key == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0))
    {
        toggleMixer();
        menuItemsChanged();  // Update View menu tick state
        return true;
    }
    if (key == juce::KeyPress ('m', juce::ModifierKeys::ctrlModifier, 0))
    {
        toggleMixer();
        menuItemsChanged();  // Update View menu tick state
        return true;
    }

    // Undo: Cmd+Z / Ctrl+Z
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        appState.editManager.undoLastAction();
        menuItemsChanged();  // Update Edit menu enabled state
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
    {
        appState.editManager.undoLastAction();
        menuItemsChanged();  // Update Edit menu enabled state
        return true;
    }

    // Redo: Cmd+Shift+Z / Ctrl+Shift+Z
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier
                                  | juce::ModifierKeys::shiftModifier, 0))
    {
        appState.editManager.redoLastAction();
        menuItemsChanged();  // Update Edit menu enabled state
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier
                                  | juce::ModifierKeys::shiftModifier, 0))
    {
        appState.editManager.redoLastAction();
        menuItemsChanged();  // Update Edit menu enabled state
        return true;
    }

    // Escape: hide Piano Roll if visible
    if (key == juce::KeyPress::escapeKey && pianoRollVisible)
    {
        hidePianoRoll();
        return true;
    }

    // Cmd+E: split selected clip(s) at playhead
    if (key == juce::KeyPress ('e', juce::ModifierKeys::commandModifier, 0) ||
        key == juce::KeyPress ('E', juce::ModifierKeys::commandModifier, 0))
        return arrangementView.keyPressed (key);

    // Cmd+C / Cmd+D / Cmd+V — forward to ArrangementView so they work regardless
    // of which component currently holds keyboard focus.
    if (key.getModifiers().isCommandDown())
    {
        const int k = key.getKeyCode();
        if (k == 'c' || k == 'C' || k == 'd' || k == 'D' || k == 'v' || k == 'V')
            return arrangementView.keyPressed (key);
    }

    // Spacebar: global play/stop — double-tap within 400ms rewinds to start.
    if (key == juce::KeyPress::spaceKey)
    {
        if (auto* edit = appState.editManager.getEdit())
        {
            auto& transport = edit->getTransport();
            const juce::int64 now = juce::Time::currentTimeMillis();
            const bool isDoubleTap = (now - lastSpaceKeyTimeMs) < kDoubleSpaceThresholdMs;
            lastSpaceKeyTimeMs = now;

            if (isDoubleTap)
            {
                // Double-tap: stop + return to start (same as Rewind button)
                transport.stop (false, false);
                transport.setPosition (tracktion::core::TimePosition::fromSeconds (0.0));
            }
            else
            {
                if (transport.isPlaying())
                    transport.stop (false, false);
                else
                    transport.play (false);
            }
        }
        return true;
    }

    return false;
}

void MainComponent::toggleMixer()
{
    mixerVisible = !mixerVisible;
    mixerView.setVisible (mixerVisible);
    updateMixerDivider();
    resized();   // Re-layout so ArrangementView expands/collapses accordingly
}

void MainComponent::updateMixerDivider()
{
    mixerDivider.mixerExpanded = mixerVisible;
    mixerDivider.repaint();
}

//==============================================================================
// Beginner world (Phase 2, Block A) — the Dial swaps which view is visible.
// Both views render over the SAME te::Edit, so switching loses no session state.
//==============================================================================

void MainComponent::applyUIMode()
{
    const bool beginner = appState.uiState.getMode() == UIStateManager::UIMode::Beginner;

    beginnerView.setVisible (beginner);

    // Pro composition — hidden in Beginner mode, restored (respecting sub-state) in Pro.
    transportBar.setVisible    (! beginner);
    arrangementView.setVisible (! beginner);
    mixerDivider.setVisible    (! beginner);
    mixerView.setVisible       (! beginner && mixerVisible);
    pluginWindowManager.getDockedPanel().setVisible (! beginner);
    if (pianoRollView != nullptr)
        pianoRollView->setVisible (! beginner && pianoRollVisible);
    pianoRollCloseButton.setVisible (! beginner && pianoRollVisible);

    if (beginner) beginnerView.toFront (false);
    else          transportBar.toFront (false);

    resized();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &appState.uiState)
    {
        applyUIMode();
        menuItemsChanged();   // refresh the View-menu "Einsteiger-Welt" tick
    }
}

void MainComponent::showPianoRoll (te::MidiClip& midiClip)
{
    // Destroy any existing piano roll, then create new one for this clip
    pianoRollView.reset();
    pianoRollView = std::make_unique<PianoRollView> (midiClip, appState.editManager);
    addAndMakeVisible (*pianoRollView);
    pianoRollVisible = true;
    pianoRollCloseButton.setVisible (true);
    pianoRollCloseButton.toFront (false);
    resized();
}

void MainComponent::hidePianoRoll()
{
    pianoRollVisible = false;
    pianoRollCloseButton.setVisible (false);
    if (pianoRollView != nullptr)
    {
        removeChildComponent (pianoRollView.get());
        pianoRollView.reset();
    }
    resized();
}

//==============================================================================
// juce::MenuBarModel implementation
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Cloudes", "File", "Edit", "View", "Song", "Export" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String& /*name*/)
{
    juce::PopupMenu menu;
    auto* edit = appState.editManager.getEdit();
    const bool hasEdit = (edit != nullptr);

    switch (index)
    {
        case 0:  // Cloudes
            menu.addItem (CloudesPreferences, "Preferences...", true);
            break;

        case 1:  // File
            menu.addItem (FileNewSession,  "New Session",     true);
            menu.addItem (FileOpen,        "Open Session...", true);
            menu.addSeparator();
            menu.addItem (FileSave,        "Save",            hasEdit);
            menu.addItem (FileSaveAs,      "Save As...",      hasEdit);
            menu.addSeparator();
            menu.addItem (FileImportAudio, "Import Audio...", hasEdit);
            break;

        case 2:  // Edit
            if (hasEdit)
            {
                auto& undoManager = appState.editManager.getUndoManager();
                menu.addItem (EditUndo,
                              "Undo " + undoManager.getUndoDescription(),
                              undoManager.canUndo());
                menu.addItem (EditRedo,
                              "Redo " + undoManager.getRedoDescription(),
                              undoManager.canRedo());
            }
            else
            {
                menu.addItem (EditUndo, "Undo", false);
                menu.addItem (EditRedo, "Redo", false);
            }
            break;

        case 3:  // View
            menu.addItem (ViewToggleBeginner, "Einsteiger-Welt",
                          true,
                          appState.uiState.getMode() == UIStateManager::UIMode::Beginner);
            menu.addSeparator();
            menu.addItem (ViewToggleMixer,
                          mixerVisible ? "Hide Mixer" : "Show Mixer",
                          true,
                          mixerVisible);
            menu.addSeparator();
            menu.addItem (ScanPlugins, "Scan Plugins...", true);
            menu.addItem (ViewReopenOnboarding, "Onboarding wiederholen...", true);
            break;

        case 4:  // Song Setup
            menu.addItem (SongSetupAudioIO, "Audio I/O Setup...", true);
            break;

        case 5:  // Export
            menu.addItem (ExportMixdown, "Export Mixdown...", hasEdit);
            menu.addItem (ExportStems,   "Export Stems...",   hasEdit);
            break;

        default:
            break;
    }
    return menu;
}

void MainComponent::menuItemSelected (int itemID, int /*topLevelIndex*/)
{
    switch (itemID)
    {
        case CloudesPreferences:
            AudioPreferencesDialog::show (appState.audioEngine);
            break;

        case FileNewSession:    handleFileNew();          break;
        case FileOpen:         handleFileOpen();         break;
        case FileSave:         handleFileSave();         break;
        case FileSaveAs:       handleFileSaveAs();       break;
        case FileImportAudio:  handleFileImportAudio();  break;

        case EditUndo:
            appState.editManager.undoLastAction();
            menuItemsChanged();
            break;

        case EditRedo:
            appState.editManager.redoLastAction();
            menuItemsChanged();
            break;

        case ViewToggleMixer:
            toggleMixer();
            menuItemsChanged();
            break;

        case ScanPlugins:
            handleScanPlugins();
            break;

        case ViewReopenOnboarding:
            handleReopenOnboarding();
            break;

        case ViewToggleBeginner:
            // Broadcasts → changeListenerCallback applies the mode + refreshes the menu.
            appState.uiState.toggleMode();
            break;

        case SongSetupAudioIO:
            AudioIOSetupDialog::show (appState.editManager.getEngine(), inputBusManager);
            break;

        case ExportMixdown:
            ExportDialog::showMixdownDialog (appState.editManager, exportPipeline);
            break;

        case ExportStems:
            ExportDialog::showStemsDialog (appState.editManager, exportPipeline);
            break;

        default:
            break;
    }
}

//==============================================================================
// File menu handlers
// Note: JUCE 8 removed the deprecated synchronous FileChooser API.
// All file dialogs use launchAsync with a callback stored as a member
// (fileChooser) so that the chooser outlives the callback lambda.
//==============================================================================

void MainComponent::handleFileNew()
{
    auto* currentEdit = appState.editManager.getEdit();
    // hasChangedSinceSaved() is the correct TE v3.2.0 method name
    if (currentEdit != nullptr && currentEdit->hasChangedSinceSaved())
    {
        // JUCE 8: showYesNoCancelBox async variant
        juce::AlertWindow::showYesNoCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "New Session",
            "Save current session before creating a new one?",
            "Save", "Don't Save", "Cancel",
            nullptr,
            juce::ModalCallbackFunction::create ([this] (int choice)
            {
                if (choice == 1)       // "Save"
                    handleFileSave();
                else if (choice == 0)  // "Cancel"
                    return;
                // choice == 2 ("Don't Save") — discard and continue
                appState.editManager.createNewEdit();
                menuItemsChanged();
            }));
    }
    else
    {
        appState.editManager.createNewEdit();
        menuItemsChanged();
    }
}

void MainComponent::handleFileOpen()
{
    // JUCE 8: async FileChooser — store chooser as member to keep it alive
    fileChooser = std::make_unique<juce::FileChooser> (
        "Open Session",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.tracktion");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result.existsAsFile())
            {
                appState.editManager.loadEdit (result);
                appState.editManager.setCurrentFile (result);
                menuItemsChanged();
            }
            fileChooser.reset();
        });
}

void MainComponent::handleFileSave()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    // If no current session file is set, fall through to Save As
    const auto file = appState.editManager.getCurrentFile();
    if (file.existsAsFile())
    {
        appState.editManager.saveEdit (file);
    }
    else
    {
        handleFileSaveAs();
    }
}

void MainComponent::handleFileSaveAs()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    // JUCE 8: async FileChooser
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save Session As",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.tracktion");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result != juce::File{})
            {
                const auto file = result.withFileExtension (".tracktion");
                appState.editManager.saveEdit (file);
                appState.editManager.setCurrentFile (file);
            }
            fileChooser.reset();
        });
}

void MainComponent::handleFileImportAudio()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto tracks = te::getAudioTracks (*edit);
    if (tracks.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "Import Audio",
            "Please add an audio track first before importing audio.");
        return;
    }

    // JUCE 8: async FileChooser
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import Audio File",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto audioFile = fc.getResult();
            if (! audioFile.existsAsFile())
            {
                fileChooser.reset();
                return;
            }

            auto* edit2 = appState.editManager.getEdit();
            if (edit2 == nullptr) { fileChooser.reset(); return; }

            auto tracks2 = te::getAudioTracks (*edit2);
            if (tracks2.isEmpty()) { fileChooser.reset(); return; }

            auto& track   = *tracks2[0];
            auto& undoMgr = appState.editManager.getUndoManager();
            undoMgr.beginNewTransaction ("Import audio file");

            using TimePos = tracktion::core::TimePosition;
            using TimeRng = tracktion::core::TimeRange;

            te::ClipPosition pos { TimeRng (TimePos::fromSeconds (0.0),
                                             TimePos::fromSeconds (30.0)), {} };
            auto* clip = track.insertNewClip (te::TrackItem::Type::wave,
                                               audioFile.getFileNameWithoutExtension(),
                                               pos, nullptr);
            if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
                waveClip->getSourceFileReference().setToDirectFileReference (audioFile, false);

            fileChooser.reset();
        });
}

//==============================================================================
// View menu handlers
//==============================================================================

void MainComponent::handleScanPlugins (bool userInitiated)
{
    // Modeless, live progress overlay (Apple-minimal — no blocking dialog).
    showScanProgress();

    // scanForPlugins launches its own background thread and returns immediately.
    // Both callbacks fire on the background thread → marshal to the message thread
    // with a SafePointer (this Component may be gone by the time they arrive).
    // Persistence (incl. the final save) is handled inside the scanner via the
    // cache file set in the constructor — no explicit savePluginList here.
    pluginScanner.scanForPlugins (
        [safe = juce::Component::SafePointer<MainComponent> (this)]
        (const PluginScanner::ScanProgress& progress)
        {
            juce::MessageManager::callAsync ([safe, progress]
            {
                if (safe == nullptr) return;
                if (safe->scanProgressPanel != nullptr)
                    safe->scanProgressPanel->update (progress);
            });
        },
        [safe = juce::Component::SafePointer<MainComponent> (this), userInitiated] ()
        {
            juce::MessageManager::callAsync ([safe, userInitiated]
            {
                if (safe == nullptr) return;

                // Register the freshly-scanned plugins with the engine so they are
                // instantiable (editor + audio) without a restart.
                safe->syncScannedPluginsToEngine();
                safe->hideScanProgress();

                if (userInitiated)
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon,
                        "Scan Complete",
                        "Plugin scan finished. The browser now shows all found plugins.",
                        "OK");
            });
        });
}

void MainComponent::showScanProgress()
{
    if (scanProgressPanel == nullptr)
    {
        scanProgressPanel = std::make_unique<ScanProgressPanel>();
        addAndMakeVisible (*scanProgressPanel);
    }
    scanProgressPanel->toFront (false);
    resized();   // position it
}

void MainComponent::hideScanProgress()
{
    scanProgressPanel.reset();
}

void MainComponent::showProAssistPalette()
{
    if (proAssistPalette != nullptr)   // ⌘K again (or re-entry) closes it
    {
        proAssistPalette.reset();
        return;
    }

    proAssistPalette = std::make_unique<ProAssistPalette>();

    proAssistPalette->targetName = [this]() -> juce::String
    {
        if (auto* t = arrangementView.getSelectedTrack())
            return t->getName();
        return "Erster Track";
    };
    proAssistPalette->onApply = [this] (IntentEngine::Intent intent) { applyAssistIntent (intent); };
    proAssistPalette->onDismiss = [this]
    {
        // Defer teardown out of the palette's own callback (avoid self-delete).
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safe == nullptr) return;
            safe->proAssistPalette.reset();
        });
    };

    addAndMakeVisible (*proAssistPalette);
    proAssistPalette->setBounds (getLocalBounds());
    proAssistPalette->toFront (true);
    proAssistPalette->reset();   // clear text, select first, grab focus
}

void MainComponent::applyAssistIntent (IntentEngine::Intent intent)
{
    // Mirror BeginnerView::runIntent: target the selected track (null → first audio track;
    // loudness/glue intents ignore the target and act on the master). The plan is real,
    // editable TE mechanic applied as ONE undo transaction via the bridge.
    auto* target = arrangementView.getSelectedTrack();
    const auto plan = appState.intentEngine.plan (intent, target);
    if (! plan.valid)
        return;

    appState.aiCommandBridge.enqueueBatch (plan.description, plan.commands);
}

void MainComponent::syncScannedPluginsToEngine()
{
    // Cloudes scans into its own PluginScanner::knownPlugins (drives the browser), but
    // Tracktion Engine instantiates plugins from engine.getPluginManager().knownPluginList
    // (ExternalPlugin::findMatchingPlugin). Mirror the scanned descriptions across so TE
    // can create real instances — otherwise third-party plugins are inert shells with no
    // editor and no audio. addType() de-dupes by identifier, so repeat calls are safe.
    auto& engineList = appState.editManager.getEngine().getPluginManager().knownPluginList;
    for (const auto& type : pluginScanner.getKnownPlugins().getTypes())
        engineList.addType (type);
}

juce::File MainComponent::getPluginListCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Cloudes")
               .getChildFile ("knownPlugins.xml");
}
