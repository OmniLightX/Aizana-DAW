#include "MainComponent.h"

#include "generator/PluginSpec.h"
#include "generator/FaustEmitter.h"
#include "generator/LocalComposer.h"
#include "generator/FaustCompiler.h"
#include "ui/aistudio/AiStudioPanel.h"
#include "ui/pluginbrowser/GeneratedPluginsPanel.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "plugins/oop/OutOfProcessPluginInstance.h"
#include "testing/UiTestHook.h"

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
    // Fired BEFORE the old Edit is destroyed: every UI owner drops its
    // references while the old tracks/plugins are still alive. Without this,
    // File>Open/New destroyed the Edit under live views → use-after-free crash
    // (live-test finding 2026-07-02: app crashed when reopening a session).
    appState.editManager.onEditWillChange = [this]
    {
        pluginWindowManager.closeAllWindows();   // windows hold te::Plugin& into the old Edit
        dockedEditorPanel.clear();               // editor holds clip refs into the old Edit
        hideCompingEditor();                     // comping editor targets an old clip
        clipEditor.clearClipboard();             // raw te::Clip* into the old Edit
        arrangementView.detachFromEdit();
        mixerView.detachFromEdit();
    };

    appState.editManager.onEditChanged = [this]
    {
        inputBusManager.initialiseDefaultBuses();
        inputBusManager.applyToEngine();
        compingEngine.attachToEdit();        // re-render active comps of the new edit
        arrangementView.attachToEdit();
        mixerView.attachToEdit();
        resized();
    };

    // The startup Edit was created by ApplicationState before this component (and
    // its InputBusManager) existed, so onEditChanged could not fire for it — seed now.
    inputBusManager.initialiseDefaultBuses();
    inputBusManager.applyToEngine();
    compingEngine.attachToEdit();

    // KI-Setup (Phase 3, Slice 4): ≥3 dropped audio files can run the stem
    // pipeline — ArrangementView offers the choice, we run analysis + apply.
    arrangementView.onStemsDropped = [this] (const juce::Array<juce::File>& files)
    {
        startStemAnalysis (files);
    };

    // Selecting a strip in the mixer selects the same track in the arrangement
    // (user request 2026-06-13: the two views must stay in sync).
    mixerView.onSelectionForwarded = [this] (te::AudioTrack* t)
    {
        arrangementView.selectTrack (t);
    };

    addAndMakeVisible (transportBar);
    addAndMakeVisible (arrangementView);
    addAndMakeVisible (mixerView);
    addAndMakeVisible (dockedEditorPanel);

    // DockedPluginPanel from PluginWindowManager — right-side panel
    addAndMakeVisible (pluginWindowManager.getDockedPanel());

    // Bottom dock: restore last state (default = Mix, CONTEXT.md mixing-first)
    loadBottomDockState();
    mixerVisible = (activeBottomDock == BottomDock::Mix);
    mixerView.setVisible (mixerVisible);
    dockedEditorPanel.setVisible (activeBottomDock == BottomDock::Edit);
    dockedEditorPanel.setDockActive (activeBottomDock == BottomDock::Edit);

    // Crash-isolate plugin scanning: a broken plugin blacklists only itself instead of
    // taking down the whole scan/app (Fix 4). Must precede any scanForPlugins() call.
    pluginScanner.enableCrashIsolatedScanning();

    // Register the out-of-process hosting format in the ENGINE's format manager (E5.3c). It is
    // inert unless a description routes to it (pluginFormatName == kOopFormatName), which only
    // happens for generated CLAP plugins when AIZANA_OOP_PLUGINS=1 (see syncScannedPluginsToEngine).
    appState.editManager.getEngine().getPluginManager().pluginFormatManager
        .addFormat (new aizana::OutOfProcessPluginFormat());

    // Cache file for incremental, crash-safe persistence DURING a scan. With this set,
    // the scanner writes the partial list periodically + at every format boundary, so a
    // large library (1000+ plugins) survives an interrupted scan and resumes next launch.
    const auto pluginCache = getPluginListCacheFile();
    pluginScanner.setPluginListCacheFile (pluginCache);

    // Load previously-scanned plugin list so the browser is populated on startup.
    pluginScanner.loadPluginList (pluginCache);

    // Register Aizana-generated plugins (the user's local plugin library) so they appear
    // in the browser/insert menu and survive restarts — the .clap files on disk are the
    // source of truth. Before syncScannedPluginsToEngine() so they're also made
    // instantiable by the engine (a saved session may reference a generated plugin).
    pluginScanner.registerGeneratedPlugins (aizana::FaustCompiler::generatedPluginsDir());

    // Live-watch the generated-plugins dir so a plugin authored by an external MCP client
    // (Claude/Codex via aizana-mcp) shows up in this running session without a restart.
    {
        const auto genDir = aizana::FaustCompiler::generatedPluginsDir();
        lastGeneratedClapCount = genDir.isDirectory()
            ? genDir.getNumberOfChildFiles (juce::File::findFiles, "*.clap") : 0;
    }
    generatedLibraryWatcher.onTick = [this] { pollGeneratedLibrary(); };
    generatedLibraryWatcher.startTimer (4000);

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

    // Edit/Mix view switcher in the TransportBar (Studio One pattern)
    transportBar.onSelectViewMode = [this] (bool mixFocus) { setViewFocus (mixFocus); };
    transportBar.isMixerShown     = [this] { return mixerVisible; };
    transportBar.isEditorShown    = [this] { return activeBottomDock == BottomDock::Edit; };
    transportBar.onMixRequested   = [this] { startMix(); };
    transportBar.onAiStudioRequested = [this] { openAiStudio(); };

    // KI-Studio live bridge (Phase 4, Slice 2): host the loopback socket so the in-app
    // agent's aizana-mcp can read tracks + mix the running session. Non-fatal if no port.
    //
    // NICHT im GUI-Testlauf: der Testmodus verkleinert die App, er vergroessert sie nie.
    // Dieser Server ist ein Loopback-Socket mit Token-Datei, also eine adressierbare
    // Tuer — waehrend ein Automat blind Knoepfe drueckt, bleibt sie zu.
    if (! aizana::uitest::isArmed())
        aiLiveServer.start();

    // M13-Diagnose: XRun-Logger — jeder hörbare Aussetzer landet mit Zeitstempel,
    // Buffer/CPU und App-Kontext in ~/Library/Aizana/logs/dropouts.log, damit die
    // Dropout-Ursache (Verdacht: Streaming von externem Volume) beweisbar wird.
    dropoutMonitor = std::make_unique<DropoutMonitor> (
        appState.audioEngine.getDeviceManager(),
        [this]() -> juce::String
        {
            juce::String s;
            if (auto* edit = appState.editManager.getEdit())
            {
                auto& t = edit->getTransport();
                s << (t.isPlaying() ? "playing" : "stopped")
                  << " pos=" << juce::String (t.getPosition().inSeconds(), 2) << "s"
                  << (((bool) t.looping) ? " loop" : "");
            }
            const auto session = appState.editManager.getCurrentFile();
            s << " session=" << session.getFullPathName();
            if (session.getFullPathName().startsWith ("/Volumes/"))
                s << " [EXTERNES VOLUME — Streaming-Verdacht M12/M13]";
            return s;
        });

    // Inspector (left dock) — pulls the arrangement selection via providers
    inspectorPanel.trackProvider = [this] { return arrangementView.getSelectedTrack(); };
    inspectorPanel.clipProvider  = [this] { return arrangementView.getPrimarySelectedClip(); };
    addChildComponent (inspectorPanel);   // hidden by default

    // Bottom bar — Edit/Mix/Browse buttons (Studio One), click empty area to
    // toggle the current dock, drag to resize; height persists across launches.
    mixerDivider.onEditClick   = [this] { setBottomDock (BottomDock::Edit); };
    mixerDivider.onMixClick    = [this] { setBottomDock (BottomDock::Mix); };
    mixerDivider.onBrowseClick = [this] { showBrowsePopup(); };
    mixerDivider.onToggleClick = [this]
    {
        // Short click on the empty bar: collapse if open, else reopen the mixer
        setBottomDock (activeBottomDock == BottomDock::None ? BottomDock::Mix
                                                            : activeBottomDock);
        menuItemsChanged();
    };
    mixerDivider.onDragDelta   = [this] (int dy)
    {
        // dy > 0 = dragged down → dock shrinks; dy < 0 = dragged up → dock grows
        mixerHeightPx -= dy;
        resized();
    };
    mixerDivider.onDragEnd = [this] { persistBottomDockState(); };
    updateMixerDivider();
    addAndMakeVisible (mixerDivider);

    // Beginner world (Phase 2, Block A) — a second view over the SAME te::Edit.
    // Added hidden; the Dial (UIStateManager) decides which view is shown, and
    // switching is lossless because both views share one Edit (UI-04).
    addChildComponent (beginnerView);

    // Pro-Assist rail (Sketch 004, A of B+A) — opt-in dockable surface. Target = the
    // arrangement's selected track (null → first); × toggles assist back off. Added
    // hidden; applyUIMode() shows it in Pro mode when assist is on.
    proAssistRail.targetProvider = [this] { return arrangementView.getSelectedTrack(); };
    proAssistRail.onClose        = [this] { appState.uiState.toggleAssist(); };
    addChildComponent (proAssistRail);

    appState.uiState.addChangeListener (this);

    // Allow this component to receive key events
    setWantsKeyboardFocus (true);

    addChildComponent (compingCloseButton);    // hidden by default
    compingCloseButton.onClick = [this] { hideCompingEditor(); };

    // Transient "Generiere…" overlay for the runtime plugin generator (G3).
    generatorStatusLabel.setJustificationType (juce::Justification::centred);
    generatorStatusLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xee1c1c1e));
    generatorStatusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addChildComponent (generatorStatusLabel);   // hidden until a generation runs

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
    // Stop the live bridge first: closes the loopback socket, joins its thread, and
    // removes the discovery file so no stale port is advertised after shutdown.
    aiLiveServer.stop();

    // Stop listening to the Dial before teardown (uiState outlives this component).
    appState.uiState.removeChangeListener (this);

    // Detach the Named-Bus edit-change callback before this component (and its
    // InputBusManager member) is destroyed. editManager lives in ApplicationState,
    // which outlives MainComponent — without this, a later createNewEdit()/loadEdit()
    // (e.g. during shutdown) would invoke a lambda capturing a dangling `this`.
    appState.editManager.onEditChanged    = nullptr;
    appState.editManager.onEditWillChange = nullptr;
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

    // Stem-separation progress card — same corner, stacked above the scan slot
    // so both can coexist (separation is a minutes-long background job).
    if (separationPanel != nullptr)
    {
        constexpr int w = 320, h = 72, margin = 16;
        separationPanel->setBounds (getWidth()  - w - margin,
                                    getHeight() - 2 * h - 2 * margin, w, h);
        separationPanel->toFront (false);
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

    // Inspector — left dock, spans the full height below the transport bar
    if (inspectorVisible)
        inspectorPanel.setBounds (area.removeFromLeft (InspectorPanel::kWidth));

    // DockedPluginPanel — right-side panel (auto-hides when empty)
    auto& dockedPanel = pluginWindowManager.getDockedPanel();
    const int dockedWidth = dockedPanel.isEmpty() ? 0 : kDockedPanelWidth;
    dockedPanel.setBounds (area.removeFromRight (dockedWidth));

    // Pro-Assist rail — opt-in, docked to the right (left of any plugin panel).
    if (proAssistRail.isVisible())
        proAssistRail.setBounds (area.removeFromRight (kAssistRailWidth));

    // Comping editor — its own slot above the bottom dock
    if (compingVisible && compingView != nullptr)
    {
        auto compingArea = area.removeFromBottom (250);
        compingCloseButton.setBounds (compingArea.getRight() - 24, compingArea.getY() + 4, 20, 20);
        compingView->setBounds (compingArea);
    }

    // Bottom bar — always visible; hosts the Edit/Mix/Browse buttons
    mixerDivider.setBounds (area.removeFromBottom (kMixerToggleBarHeight));

    // Bottom dock — ONE slot shared by the Docked Editor (Edit) and the Mixer
    // (Mix); height is user-draggable and persisted.
    if (activeBottomDock != BottomDock::None)
    {
        const int totalAvail = area.getHeight();
        if (mixerHeightPx == 0)
            mixerHeightPx = (int)(totalAvail * 0.50f);

        const int maxDockHeight = juce::jmax (kMixerMinHeight, totalAvail - kArrangementMinHeight);
        mixerHeightPx = juce::jlimit (kMixerMinHeight, maxDockHeight, mixerHeightPx);
        auto dockArea = area.removeFromBottom (mixerHeightPx);

        if (activeBottomDock == BottomDock::Mix)
            mixerView.setBounds (dockArea);
        else
            dockedEditorPanel.setBounds (dockArea);
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

    // Cmd+I / Ctrl+I: toggle the Inspector dock
    if (key == juce::KeyPress ('i', juce::ModifierKeys::commandModifier, 0) ||
        key == juce::KeyPress ('i', juce::ModifierKeys::ctrlModifier, 0))
    {
        toggleInspector();
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

    // Escape: close the docked editor if open
    if (key == juce::KeyPress::escapeKey && activeBottomDock == BottomDock::Edit)
    {
        hidePianoRoll();
        return true;
    }

    // Escape: hide Comping editor if visible
    if (key == juce::KeyPress::escapeKey && compingVisible)
    {
        hideCompingEditor();
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

    // Delete/Backspace — same global forward. MacBook keyboards send BACKSPACE for
    // their "Delete" key, and focus is often on the mixer/dock when the user hits it;
    // without this forward the key silently did nothing outside the arrangement.
    // (Text fields never reach here — a focused TextEditor consumes its keys first.)
    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey)
        return arrangementView.deleteSelection();

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
    setBottomDock (BottomDock::Mix);   // toggle semantics: closes if already open
}

//==============================================================================
// Bottom dock (Studio One "Edit | Mix | Browse")
//==============================================================================

void MainComponent::setBottomDock (BottomDock requested)
{
    // Clicking the active button closes the dock (Studio One behaviour)
    openBottomDock (requested == activeBottomDock ? BottomDock::None : requested);
}

void MainComponent::openBottomDock (BottomDock next)
{
    activeBottomDock = next;
    mixerVisible = (next == BottomDock::Mix);

    const bool beginner = appState.uiState.getMode() == UIStateManager::UIMode::Beginner;
    mixerView.setVisible (! beginner && mixerVisible);
    dockedEditorPanel.setVisible (! beginner && next == BottomDock::Edit);
    dockedEditorPanel.setDockActive (next == BottomDock::Edit);

    updateMixerDivider();
    persistBottomDockState();
    resized();
    menuItemsChanged();
}

void MainComponent::persistBottomDockState()
{
    auto& props = appState.editManager.getEngine().getPropertyStorage().getPropertiesFile();
    props.setValue ("bottomDock", activeBottomDock == BottomDock::Edit ? "edit"
                                : activeBottomDock == BottomDock::Mix  ? "mix" : "none");
    if (mixerHeightPx > 0)
        props.setValue ("bottomDockHeight", mixerHeightPx);
    props.saveIfNeeded();
}

void MainComponent::loadBottomDockState()
{
    auto& props = appState.editManager.getEngine().getPropertyStorage().getPropertiesFile();
    const auto s = props.getValue ("bottomDock", "mix");
    activeBottomDock = (s == "edit") ? BottomDock::Edit
                     : (s == "none") ? BottomDock::None
                                     : BottomDock::Mix;
    mixerHeightPx = props.getIntValue ("bottomDockHeight", 0);
}

void MainComponent::showBrowsePopup()
{
    // "Browse" — the existing plugin browser. Picking a plugin inserts it at
    // the end of the selected track's chain (like clicking its last "+" slot).
    auto* track = arrangementView.getSelectedTrack();
    if (track == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Browse",
            juce::String::fromUTF8 ("W\xc3\xa4hle zuerst eine Spur aus, um Plugins aus dem Browser einzuf\xc3\xbcgen."));
        return;
    }

    PluginBrowserPopup::showForComponent (
        mixerDivider,
        pluginScanner.getKnownPlugins(),
        [this, trackId = track->itemID] (const juce::PluginDescription& desc)
        {
            auto* edit = appState.editManager.getEdit();
            if (edit == nullptr)
                return;
            for (auto* t : te::getAudioTracks (*edit))
                if (t->itemID == trackId)
                {
                    const int idx = pluginManager.chainInsertIndexForUserSlot (*t, 1 << 20);
                    if (auto* p = pluginManager.insertPlugin (*t, desc, idx))
                        pluginWindowManager.openPluginWindow (*p);
                    return;
                }
        });
}

void MainComponent::toggleInspector()
{
    inspectorVisible = ! inspectorVisible;
    inspectorPanel.setVisible (inspectorVisible
                               && appState.uiState.getMode() != UIStateManager::UIMode::Beginner);
    resized();
    menuItemsChanged();
}

void MainComponent::setViewFocus (bool mixFocus)
{
    // TransportBar Edit/Mix — same dock semantics as the bottom-bar buttons
    setBottomDock (mixFocus ? BottomDock::Mix : BottomDock::Edit);
}

void MainComponent::updateMixerDivider()
{
    mixerDivider.mixerExpanded = (activeBottomDock != BottomDock::None);
    mixerDivider.setActiveDock (activeBottomDock == BottomDock::Edit ? 1
                              : activeBottomDock == BottomDock::Mix  ? 2 : 0);
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
    // Pro-Assist rail: only in Pro mode, and only when assist is toggled on (the Dial's
    // assist flag — Pro default is off). Its own visibilityChanged() starts/stops the
    // observations timer.
    proAssistRail.setVisible (! beginner && appState.uiState.isAssistEnabled());
    dockedEditorPanel.setVisible (! beginner && activeBottomDock == BottomDock::Edit);
    if (compingView != nullptr)
        compingView->setVisible (! beginner && compingVisible);
    compingCloseButton.setVisible (! beginner && compingVisible);
    inspectorPanel.setVisible (! beginner && inspectorVisible);

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
    // MIDI editing lives in the bottom Docked Editor now (Studio One "Edit"):
    // open the dock and show this clip immediately (selection follows anyway).
    openBottomDock (BottomDock::Edit);
    dockedEditorPanel.showClip (&midiClip);
}

void MainComponent::hidePianoRoll()
{
    // Legacy entry point (Esc, comping editor opening): close the editor dock.
    if (activeBottomDock == BottomDock::Edit)
        openBottomDock (BottomDock::None);
}

void MainComponent::showCompingEditor (te::WaveAudioClip& clip)
{
    hidePianoRoll();   // shared bottom dock slot

    compingView.reset();
    compingView = std::make_unique<CompingView> (clip, appState.editManager, compingEngine);
    compingView->onCloseRequest = [this]
    {
        // Deleting the clip (or undoing the recording) closes the editor. The
        // notification arrives mid-ValueTree-callback — defer the teardown.
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe == nullptr) return;
                safe->hideCompingEditor();
            });
    };
    addAndMakeVisible (*compingView);
    compingVisible = true;
    compingCloseButton.setVisible (true);
    compingCloseButton.toFront (false);
    resized();
}

void MainComponent::hideCompingEditor()
{
    compingVisible = false;
    compingCloseButton.setVisible (false);
    if (compingView != nullptr)
    {
        removeChildComponent (compingView.get());
        compingView.reset();
    }
    resized();
}

//==============================================================================
// juce::MenuBarModel implementation
//==============================================================================

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "Aizana", "File", "Edit", "View", "Song", "Export" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String& /*name*/)
{
    juce::PopupMenu menu;
    auto* edit = appState.editManager.getEdit();
    const bool hasEdit = (edit != nullptr);

    switch (index)
    {
        case 0:  // Aizana
            menu.addItem (AizanaPreferences, "Preferences...", true);
            break;

        case 1:  // File
            menu.addItem (FileNewSession,  "New Session",     true);
            menu.addItem (FileOpen,        "Open Session...", true);
            menu.addSeparator();
            menu.addItem (FileSave,        "Save",            hasEdit);
            menu.addItem (FileSaveAs,      "Save As...",      hasEdit);
            menu.addSeparator();
            menu.addItem (FileImportAudio, "Import Audio...", hasEdit);
            menu.addItem (FileImportStems,
                          juce::String::fromUTF8 ("Stems importieren (KI-Setup)…"), hasEdit);
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
            menu.addItem (ViewToggleInspector,
                          inspectorVisible ? "Inspector ausblenden" : "Inspector anzeigen",
                          true,
                          inspectorVisible);
            menu.addSeparator();
            menu.addItem (ScanPlugins, "Scan Plugins...", true);
            menu.addItem (ViewReopenOnboarding, "Onboarding wiederholen...", true);
            break;

        case 4:  // Song Setup
        {
            menu.addItem (SongSetupAudioIO, "Audio I/O Setup...", true);
            menu.addItem (SongSeparateStems,
                          juce::String::fromUTF8 ("Song in Stems trennen (KI)…"),
                          hasEdit && ! stemSeparationEngine.isBusy());
            menu.addItem (SongAddMarker,
                          juce::String::fromUTF8 ("Marker am Playhead setzen"), hasEdit);
            menu.addSeparator();
            menu.addItem (SongMixAI,
                          juce::String::fromUTF8 ("KI-Mix (Heuristik)…"), hasEdit);
            menu.addItem (SongGeneratePlugin,
                          juce::String::fromUTF8 ("Plugin generieren…"), hasEdit);
            menu.addItem (SongAiStudio,
                          juce::String::fromUTF8 ("KI-Studio (Chat: Plugins bauen lassen)…"));
            menu.addItem (SongManagePlugins,
                          juce::String::fromUTF8 ("Aizana-Plugins verwalten…"));
            menu.addSeparator();
            menu.addItem (SongMusicalTyping,
                          juce::String::fromUTF8 ("Tastatur spielen (Musical Typing)"),
                          hasEdit, musicalTypingWindow != nullptr);

            // KI-Referenz (REF-01/02/04): genre profile or analysed track.
            // The active profile's LUFS is shown as INFO — never enforced.
            juce::PopupMenu refMenu;
            const auto active = getActiveReferenceProfile();

            const auto genres = ReferenceProfile::genreNames();
            for (int i = 0; i < genres.size(); ++i)
                refMenu.addItem (SongRefGenreBase + i, genres[i], hasEdit,
                                 active.isValid() && ! active.fromReferenceTrack
                                     && active.name == genres[i]);
            refMenu.addSeparator();
            refMenu.addItem (SongRefAnalyze,
                             juce::String::fromUTF8 ("Referenztrack analysieren…"), hasEdit,
                             active.fromReferenceTrack);
            refMenu.addItem (SongRefClear, "Referenz entfernen", hasEdit && active.isValid());

            if (active.isValid())
            {
                refMenu.addSeparator();
                refMenu.addItem (9999,
                                 juce::String::fromUTF8 ("Aktiv: ") + active.name
                                     + "  (" + juce::String (active.targetLufs, 1)
                                     + " LUFS als Richtwert)",
                                 false);   // disabled = pure info (REF-04)
            }
            menu.addSubMenu (juce::String::fromUTF8 ("KI-Referenz"), refMenu);
        }
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
    // KI-Referenz genre profiles use a contiguous ID range (REF-02)
    if (itemID >= SongRefGenreBase
        && itemID < SongRefGenreBase + ReferenceProfile::genreNames().size())
    {
        setActiveReferenceProfile (ReferenceProfile::genre (
            ReferenceProfile::genreNames()[itemID - SongRefGenreBase]));
        menuItemsChanged();
        return;
    }

    switch (itemID)
    {
        case AizanaPreferences:
            AudioPreferencesDialog::show (appState.audioEngine);
            break;

        case FileNewSession:    handleFileNew();          break;
        case FileOpen:         handleFileOpen();         break;
        case FileSave:         handleFileSave();         break;
        case FileSaveAs:       handleFileSaveAs();       break;
        case FileImportAudio:  handleFileImportAudio();  break;
        case FileImportStems:  handleFileImportStems();  break;

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

        case ViewToggleInspector:
            toggleInspector();
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

        case SongSeparateStems:
            handleSongSeparateStems();
            break;

        case SongAddMarker:
            arrangementView.addMarkerAtPlayhead();
            break;

        case SongMixAI:
            startMix();
            break;

        case SongGeneratePlugin:
            startRuntimeGenerator();
            break;
        case SongManagePlugins:
            openGeneratedPluginsManager();
            break;

        case SongMusicalTyping:
            toggleMusicalTyping();
            break;

        case SongAiStudio:
            openAiStudio();
            break;

        case SongRefAnalyze:
            handleAnalyzeReferenceTrack();
            break;

        case SongRefClear:
            clearActiveReferenceProfile();
            menuItemsChanged();
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
    // Accept .tracktionedit too — autosaves and unsaved sessions use that
    // extension; filtering them out looked like "file type not recognised".
    fileChooser = std::make_unique<juce::FileChooser> (
        "Open Session",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.tracktion;*.tracktionedit");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result.existsAsFile())
                openSessionFile (result);

            fileChooser.reset();
        });
}

void MainComponent::toggleMusicalTyping()
{
    if (musicalTypingWindow != nullptr)
    {
        musicalTypingWindow.reset();
        menuItemsChanged();
        return;
    }

    // Die Spur wird bei JEDER Note frisch erfragt — so folgt das Fenster der
    // Auswahl im Arrangement, ohne dass man es neu aufmachen muss.
    musicalTypingWindow = std::make_unique<MusicalTypingWindow> (
        [this]() -> te::Track*
        {
            if (auto* t = arrangementView.getSelectedTrack())
                return t;

            // Keine Auswahl: die erste Audiospur nehmen — sonst spielt man ins
            // Leere und weiss nicht warum.
            if (auto* edit = appState.editManager.getEdit())
            {
                auto tracks = te::getAudioTracks (*edit);
                if (! tracks.isEmpty())
                    return tracks.getFirst();
            }
            return nullptr;
        });

    musicalTypingWindow->onClose = [this]
    {
        juce::MessageManager::callAsync ([this]
        {
            musicalTypingWindow.reset();
            menuItemsChanged();
        });
    };

    menuItemsChanged();
}

bool MainComponent::openSessionFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    if (appState.editManager.loadEdit (file))
    {
        appState.editManager.setCurrentFile (file);
        menuItemsChanged();
        return true;
    }

    // Parse failure keeps the current session intact — tell the
    // user instead of silently doing nothing (or crashing).
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::WarningIcon,
        "Open Session",
        juce::String::fromUTF8 ("Die Datei konnte nicht als Aizana-Session gelesen werden:\n")
            + file.getFullPathName());
    return false;
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
// KI-Setup: Stems importieren (Phase 3, Iteration 1, Slice 4)
//==============================================================================

void MainComponent::handleFileImportStems()
{
    if (appState.editManager.getEdit() == nullptr)
        return;

    fileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Stems auswählen (mehrere Dateien)"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            juce::Array<juce::File> files;
            for (const auto& f : fc.getResults())
                if (f.existsAsFile())
                    files.add (f);

            fileChooser.reset();

            if (! files.isEmpty())
                startStemAnalysis (files);
        });
}

void MainComponent::startStemAnalysis (const juce::Array<juce::File>& files)
{
    // Analysis reads files + runs FFTs — worker thread, never the message thread.
    // Result marshals back via callAsync with a SafePointer (window may close).
    // The active reference profile (REF-01/02) is read HERE on the message
    // thread and captured by value for the worker.
    juce::Thread::launch (
        [files, reference = getActiveReferenceProfile(),
         safe = juce::Component::SafePointer<MainComponent> (this)]()
        {
            const auto stems = StemAnalyzer::analyzeFiles (files);
            const auto plan  = SessionPlanner::plan (stems,
                                                     reference.isValid() ? &reference : nullptr);

            juce::MessageManager::callAsync ([safe, plan]()
            {
                if (safe == nullptr)
                    return;
                safe->onStemPlanReady (plan);
            });
        });
}

void MainComponent::onStemPlanReady (const SessionPlan& plan)
{
    if (plan.tracks.isEmpty())
        return;

    // Decision E3 (2026-06-12): Beginner = instant magic + one undo;
    // Pro = preview table (file → instrument → group → gain, editable).
    if (appState.uiState.getMode() == UIStateManager::UIMode::Beginner)
    {
        stemImportEngine.applyPlan (plan);
        return;
    }

    StemImportDialog::show (*this, plan,
        [safe = juce::Component::SafePointer<MainComponent> (this)] (const SessionPlan& edited)
        {
            if (safe == nullptr)
                return;
            safe->stemImportEngine.applyPlan (edited);
        });
}

//==============================================================================
// KI-Mix (Phase 4, Iteration 1, Slice 4)
//==============================================================================

void MainComponent::startMix()
{
    // Scan TE structure on the message thread (cheap, no I/O); measure + mix on a
    // worker (file decoding). Result marshals back via callAsync + SafePointer.
    auto scan = mixApplyEngine.scanTracks();
    if (scan.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "KI-Mix",
            juce::String::fromUTF8 ("Keine Spuren zum Mischen gefunden. Importiere zuerst Stems."));
        return;
    }

    // Local ML (Slice 5a): drop a model at ~/Library/Aizana/
    // models/mix_gain_trim.onnx and the mix consults it (CoreML/Neural Engine);
    // absent → pure heuristic. Load + run on the SAME worker (no load/run race).
    const auto modelFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("Aizana/models/mix_gain_trim.onnx");

    juce::Thread::launch (
        [scan = std::move (scan), reference = getActiveReferenceProfile(), modelFile,
         engine = &appState.aiEngine,
         safe = juce::Component::SafePointer<MainComponent> (this)]()
        {
            if (auto* b = engine->getBackend(); b != nullptr && ! b->isAvailable())
                engine->tryLoadModel (modelFile);

            const auto profiles = MixApplyEngine::profilesFromScan (scan);
            const auto plan = aizana::MixingEngine::mix (
                profiles, reference.isValid() ? &reference : nullptr, {}, engine->getBackend());

            juce::MessageManager::callAsync ([safe, plan, profiles]()
            {
                if (safe == nullptr)
                    return;
                safe->onMixPlanReady (plan, profiles);
            });
        });
}

void MainComponent::onMixPlanReady (const aizana::MixPlan& plan,
                                    const std::vector<aizana::TrackMixProfile>& profiles)
{
    if (plan.isEmpty())
        return;

    // Active genre/reference labels the example (helps a model condition on style).
    const auto ref = getActiveReferenceProfile();
    const juce::String genre = ref.isValid() ? ref.name : juce::String();

    // Beginner = instant mix + one undo; Pro = review dialog (moves + reasons,
    // each de-selectable) — mirrors the stem-import E3 split. Both feed the
    // training-data flywheel (Beginner = auto-accepted, weaker; Pro = real verdict).
    if (appState.uiState.getMode() == UIStateManager::UIMode::Beginner)
    {
        mixApplyEngine.applyPlan (plan);
        aizana::MixDataLogger::logReview (profiles, plan, plan,
                                           { "heuristic", "beginner", genre, "" });
        return;
    }

    MixPreviewDialog::show (*this, plan,
        [safe = juce::Component::SafePointer<MainComponent> (this),
         proposed = plan, profiles, genre] (const aizana::MixPlan& edited)
        {
            if (safe == nullptr)
                return;
            safe->mixApplyEngine.applyPlan (edited);
            // The Pro user's keep/drop per move = the strong training signal.
            aizana::MixDataLogger::logReview (profiles, proposed, edited,
                                               { "heuristic", "pro", genre, "" });
        });
}

//==============================================================================
// Plugin Generator — RUNTIME path (Phase 4 / Generator G3): describe → local composer
// writes Faust → FaustCompiler builds a brand-new .clap (background) → load on track.
//==============================================================================

void MainComponent::startRuntimeGenerator()
{
    auto* track = arrangementView.getSelectedTrack();
    if (track == nullptr)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Wähle zuerst eine Spur, auf die das Plugin soll."));
        return;
    }

    if (generatorBusy)
        return;   // a generation is already running

    // The runtime generator shells out to `faust` + `clang` — tell the user up front if the
    // toolchain isn't installed instead of failing after they've typed a description.
    if (! aizana::FaustCompiler::isAvailable())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Der Plugin-Generator braucht Faust. Installier es einmalig "
                                    "per Terminal:\n\n    brew install faust\n\n"
                                    "(Eine eigene Faust-Bibliothek wird mit dem Release gebündelt.)"));
        return;
    }

    const auto trackID = track->itemID;

    auto aw = std::make_unique<juce::AlertWindow> (
        juce::String::fromUTF8 ("Neues Plugin generieren"),
        juce::String::fromUTF8 ("Beschreibe dein Plugin — der Generator baut die DSP daraus neu.\n"
                                "Z. B. „warmer Tape-Sound mit etwas Reverb\", „Bus-Glue-Kompressor "
                                "mit Air\", „Tremolo mit Filter\"."),
        juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("desc", juce::String(), juce::String::fromUTF8 ("Beschreibung:"));
    aw->addButton (juce::String::fromUTF8 ("Generieren"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setVisible (true);

    auto* awPtr = aw.get();
    awPtr->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<MainComponent> (this),
             awPtr, owner = std::move (aw), trackID] (int result) mutable
            {
                const auto text = awPtr->getTextEditorContents ("desc");
                owner.reset();   // close + delete the window now (we own it)

                if (safe == nullptr || result == 0 || text.trim().isEmpty())
                    return;

                safe->runGeneratorFromPrompt (text, trackID);
            }),
        false);   // we delete the window ourselves via the captured unique_ptr
}

void MainComponent::runGeneratorFromPrompt (const juce::String& prompt, te::EditItemID trackID)
{
    if (generatorBusy)
        return;

    // Compose + emit on the message thread (pure, instant); compile on a worker (file I/O +
    // spawns faust/clang → seconds). Result marshals back via callAsync + SafePointer.
    const auto spec = aizana::LocalComposer::compose (prompt);
    const auto src  = aizana::FaustEmitter::emit (spec);

    generatorBusy = true;

    // Transient on-canvas status so the few-second compile doesn't look like a hang.
    generatorStatusLabel.setText (
        juce::String::fromUTF8 ("Generiere „") + spec.name
            + juce::String::fromUTF8 ("\" … (Faust kompiliert, kann ein paar Sekunden dauern)"),
        juce::dontSendNotification);
    generatorStatusLabel.setBounds (getLocalBounds().withSizeKeepingCentre (520, 60));
    generatorStatusLabel.setVisible (true);
    generatorStatusLabel.toFront (false);

    juce::Thread::launch (
        [src, name = spec.name, trackID,
         safe = juce::Component::SafePointer<MainComponent> (this)]()
        {
            const auto r = aizana::FaustCompiler::compile (src, name);   // background thread

            juce::MessageManager::callAsync (
                [safe, ok = r.ok, path = r.clapFile.getFullPathName(), log = r.log, name, trackID]()
                {
                    if (safe == nullptr)
                        return;
                    safe->onGeneratedPluginReady (ok, path, log, name, trackID);
                });
        });
}

void MainComponent::onGeneratedPluginReady (bool ok, const juce::String& clapPath,
                                            const juce::String& log, const juce::String& pluginName,
                                            te::EditItemID trackID)
{
    generatorBusy = false;
    generatorStatusLabel.setVisible (false);

    if (! ok)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Konnte das Plugin nicht bauen.\n\n")
                + log.substring (juce::jmax (0, log.length() - 1200)));   // tail of the log
        return;
    }

    // Add the freshly built plugin to the user's library (browser/insert menu) and make it
    // instantiable by the engine — independent of whether the target track still exists, so
    // it's reusable on any track and after restart. PluginBrowserPopup polls the list, so it
    // shows up immediately without a manual rescan.
    pluginScanner.registerGeneratedPlugins (aizana::FaustCompiler::generatedPluginsDir());
    syncScannedPluginsToEngine();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    // Re-resolve the track by id — the compile took seconds and the selection/edit may have
    // changed. Fall back to the current selection; warn if there's nowhere to put it.
    te::AudioTrack* track = nullptr;
    for (auto* t : te::getAudioTracks (*edit))
        if (t->itemID == trackID)
            { track = t; break; }
    if (track == nullptr)
        track = arrangementView.getSelectedTrack();
    if (track == nullptr)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Plugin generieren",
            juce::String::fromUTF8 ("„") + pluginName
                + juce::String::fromUTF8 ("\" wurde gebaut, aber die Zielspur existiert nicht mehr.\n"
                                          "Liegt in der Plugin-Library."));
        return;
    }

    // Insert via the standard ExternalPlugin path (CLAP is registered with the engine format
    // manager in EditManager) and open its editor.
    ClapPluginFormat clap;
    juce::OwnedArray<juce::PluginDescription> descs;
    clap.findAllTypesForFile (descs, clapPath);
    if (descs.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Das generierte Plugin „") + pluginName
                + juce::String::fromUTF8 ("\" konnte nicht geladen werden."));
        return;
    }

    if (auto* plugin = pluginManager.insertPlugin (*track, *descs[0]))
        pluginWindowManager.openPluginWindow (*plugin);
}

//==============================================================================
// Plugin Generator (Phase 4 / Iteration B, B2) — describe/pick → load .clap on track
//==============================================================================

void MainComponent::startPluginGenerator()
{
    auto* track = arrangementView.getSelectedTrack();
    if (track == nullptr)
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Wähle zuerst eine Spur, auf die das Plugin soll."));
        return;
    }

    juce::PopupMenu menu;
    const auto& tmpls = aizana::PluginGenerator::templates();
    for (int i = 0; i < (int) tmpls.size(); ++i)
        menu.addItem (i + 1, tmpls[(size_t) i].displayName + "  —  " + tmpls[(size_t) i].description);
    menu.addSeparator();
    menu.addItem (1000, juce::String::fromUTF8 ("Beschreiben…"));

    menu.showMenuAsync (juce::PopupMenu::Options{},
        [safe = juce::Component::SafePointer<MainComponent> (this)] (int choice)
        {
            if (safe == nullptr || choice == 0)
                return;

            if (choice == 1000)
            {
                safe->describeAndGenerate();
                return;
            }

            auto* t = safe->arrangementView.getSelectedTrack();
            if (t == nullptr)
                return;

            const auto& list = aizana::PluginGenerator::templates();
            const int idx = choice - 1;
            if (idx >= 0 && idx < (int) list.size())
                safe->generatePluginOnTrack (list[(size_t) idx], *t);
        });
}

void MainComponent::describeAndGenerate()
{
    auto aw = std::make_unique<juce::AlertWindow> (
        juce::String::fromUTF8 ("Plugin generieren"),
        juce::String::fromUTF8 ("Beschreibe das Plugin (z. B. „mehr Verzerrung\", „dunkler\", „lauter\"):"),
        juce::MessageBoxIconType::QuestionIcon);
    aw->addTextEditor ("desc", juce::String(), juce::String::fromUTF8 ("Beschreibung:"));
    aw->addButton (juce::String::fromUTF8 ("Generieren"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setVisible (true);

    auto* awPtr = aw.get();
    awPtr->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<MainComponent> (this),
             awPtr, owner = std::move (aw)] (int result) mutable
            {
                const auto text = awPtr->getTextEditorContents ("desc");
                owner.reset();   // close + delete the window now (we own it)

                if (safe == nullptr || result == 0)
                    return;

                const auto* m = aizana::PluginGenerator::match (text);
                auto* t = safe->arrangementView.getSelectedTrack();
                if (t == nullptr)
                    return;

                if (m == nullptr)
                {
                    juce::NativeMessageBox::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon, "Plugin generieren",
                        juce::String::fromUTF8 ("Konnte nichts zuordnen. Versuch z. B. „Verzerrung\", "
                                                "„Filter\"/„dunkler\" oder „lauter\"."));
                    return;
                }
                safe->generatePluginOnTrack (*m, *t);
            }),
        false);   // we delete the window ourselves via the captured unique_ptr
}

void MainComponent::generatePluginOnTrack (const aizana::GeneratorTemplate& tmpl, te::AudioTrack& track)
{
    const auto clapFile = aizana::PluginGenerator::clapFileFor (tmpl);
    if (! clapFile.existsAsFile())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Generiertes Plugin nicht gefunden:\n") + clapFile.getFullPathName()
                + juce::String::fromUTF8 ("\n\n(App ohne Faust-Generator gebaut?)"));
        return;
    }

    // Resolve a PluginDescription for the bundled .clap, then insert via the standard
    // ExternalPlugin path (CLAP is registered with the engine format manager in EditManager).
    ClapPluginFormat clap;
    juce::OwnedArray<juce::PluginDescription> descs;
    clap.findAllTypesForFile (descs, clapFile.getFullPathName());
    if (descs.isEmpty())
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Plugin generieren",
            juce::String::fromUTF8 ("Konnte das generierte Plugin nicht laden."));
        return;
    }

    if (auto* plugin = pluginManager.insertPlugin (track, *descs[0]))
        pluginWindowManager.openPluginWindow (*plugin);
}

//==============================================================================
// KI-Referenz (Phase 3, Iteration 2 — REF-01/02/04)
//==============================================================================

ReferenceProfile MainComponent::getActiveReferenceProfile() const
{
    if (auto* edit = appState.editManager.getEdit())
        return ReferenceProfile::fromValueTree (
            edit->state.getChildWithName (juce::Identifier (ReferenceProfile::kStateType)));
    return {};
}

void MainComponent::setActiveReferenceProfile (const ReferenceProfile& profile)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr || ! profile.isValid())
        return;

    auto& um = appState.editManager.getUndoManager();
    um.beginNewTransaction ("Set reference profile");

    auto existing = edit->state.getChildWithName (
        juce::Identifier (ReferenceProfile::kStateType));
    if (existing.isValid())
        edit->state.removeChild (existing, &um);
    edit->state.addChild (profile.toValueTree(), -1, &um);
}

void MainComponent::clearActiveReferenceProfile()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto existing = edit->state.getChildWithName (
        juce::Identifier (ReferenceProfile::kStateType));
    if (! existing.isValid())
        return;

    auto& um = appState.editManager.getUndoManager();
    um.beginNewTransaction ("Clear reference profile");
    edit->state.removeChild (existing, &um);
}

void MainComponent::handleAnalyzeReferenceTrack()
{
    if (appState.editManager.getEdit() == nullptr)
        return;

    fileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Referenztrack auswählen"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            fileChooser.reset();
            if (! file.existsAsFile())
                return;

            ReferenceAnalyzer::analyzeAsync (file,
                [safe = juce::Component::SafePointer<MainComponent> (this)]
                (ReferenceProfile profile)
                {
                    if (safe == nullptr)
                        return;

                    if (! profile.isValid())
                    {
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon, "KI-Referenz",
                            "Referenztrack konnte nicht analysiert werden.");
                        return;
                    }

                    safe->setActiveReferenceProfile (profile);
                    safe->menuItemsChanged();

                    // REF-04: values are presented as reference INFORMATION.
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon, "KI-Referenz",
                        juce::String::fromUTF8 ("Referenz gesetzt: ") + profile.name
                            + "\n\nLautheit (Richtwert): "
                            + juce::String (profile.targetLufs, 1) + " LUFS\n"
                            + juce::String::fromUTF8 ("Klangbalance (Tendenz): Bässe ")
                            + juce::String (profile.lowDb, 1) + " dB, Mitten "
                            + juce::String (profile.midDb, 1) + " dB, "
                            + juce::String::fromUTF8 ("Höhen ")
                            + juce::String (profile.highDb, 1) + " dB\n\n"
                            + juce::String::fromUTF8 (
                                "Die KI nutzt diese Werte als Orientierung beim Setup "
                                "— nie als Pflicht-Ziel."));
                });
        });
}

//==============================================================================
// Stem-Separation: Song → 6 Stems → KI-Setup (Phase 3, Spike S1 GO)
//==============================================================================

void MainComponent::handleSongSeparateStems()
{
    if (appState.editManager.getEdit() == nullptr || stemSeparationEngine.isBusy())
        return;

    fileChooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Song auswählen (wird in Stems getrennt)"),
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.mp3;*.flac;*.ogg");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto song = fc.getResult();
            fileChooser.reset();
            if (song.existsAsFile())
                startStemSeparationFlow (song);
        });
}

void MainComponent::startStemSeparationFlow (const juce::File& song)
{
    using Model = StemSeparationEngine::Model;

    if (! StemSeparationEngine::isModelInstalled (Model::fast6Stem))
    {
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "KI-Stem-Trennung",
            juce::String::fromUTF8 ("Für die Stem-Trennung wird einmalig das "
                                    "KI-Modell heruntergeladen (53 MB).\n\n"
                                    "Jetzt herunterladen?"),
            "Herunterladen", "Abbrechen", nullptr,
            juce::ModalCallbackFunction::create (
                [safe = juce::Component::SafePointer<MainComponent> (this), song] (int result)
                {
                    if (safe != nullptr && result != 0)
                        safe->downloadSeparationModel (song);
                }));
        return;
    }

    // Both models present → let the user pick the trade-off (Spike S1 numbers).
    if (StemSeparationEngine::isModelInstalled (Model::fineTuned4Stem))
    {
        juce::PopupMenu m;
        m.addItem (1, juce::String::fromUTF8 ("Schnell — 6 Spuren (≈1,7× Songlänge)"));
        m.addItem (2, juce::String::fromUTF8 ("Beste Qualität — 4 Spuren (≈7× Songlänge)"));
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [safe = juce::Component::SafePointer<MainComponent> (this), song] (int result)
            {
                if (safe == nullptr || result == 0)
                    return;
                safe->runStemSeparation (song, result == 2 ? Model::fineTuned4Stem
                                                           : Model::fast6Stem);
            });
        return;
    }

    runStemSeparation (song, Model::fast6Stem);
}

void MainComponent::downloadSeparationModel (const juce::File& songToContinueWith)
{
    using Model = StemSeparationEngine::Model;

    // Im GUI-Testlauf gesperrt: 53 MB aus dem Netz sind ein Ausgang aus dem Prozess.
    if (aizana::uitest::isArmed())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "KI-Stem-Trennung", "Im GUI-Testlauf ist der Modell-Download gesperrt.");
        return;
    }

    StemSeparationEngine::modelsDirectory().createDirectory();
    auto       url    = StemSeparationEngine::requiredModelURLs  (Model::fast6Stem)[0];
    const auto target = StemSeparationEngine::requiredModelFiles (Model::fast6Stem)[0];

    modelDownloadTask = url.downloadToFile (target, juce::URL::DownloadTaskOptions());
    if (modelDownloadTask == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "KI-Stem-Trennung", "Download konnte nicht gestartet werden.");
        return;
    }

    showSeparationProgress();
    separationPanel->update (0.0f, juce::String::fromUTF8 ("KI-Modell wird geladen (53 MB)…"));
    pollSeparationModelDownload (songToContinueWith);
}

void MainComponent::pollSeparationModelDownload (const juce::File& songToContinueWith)
{
    if (modelDownloadTask == nullptr)
        return;

    if (! modelDownloadTask->isFinished())
    {
        const auto total = modelDownloadTask->getTotalLength();
        const float p = total > 0
            ? (float) modelDownloadTask->getLengthDownloaded() / (float) total : 0.0f;
        if (separationPanel != nullptr)
            separationPanel->update (p, juce::String::fromUTF8 ("KI-Modell wird geladen (53 MB)…"));

        juce::Timer::callAfterDelay (250,
            [safe = juce::Component::SafePointer<MainComponent> (this), songToContinueWith]()
            {
                if (safe != nullptr)
                    safe->pollSeparationModelDownload (songToContinueWith);
            });
        return;
    }

    const bool failed = modelDownloadTask->hadError();
    modelDownloadTask.reset();

    if (failed)
    {
        hideSeparationProgress();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "KI-Stem-Trennung",
            juce::String::fromUTF8 ("Modell-Download fehlgeschlagen — bitte "
                                    "Internetverbindung prüfen und erneut versuchen."));
        return;
    }

    startStemSeparationFlow (songToContinueWith);   // model now installed → proceed
}

void MainComponent::runStemSeparation (const juce::File& song, StemSeparationEngine::Model model)
{
    const auto outDir = song.getParentDirectory()
                            .getChildFile (song.getFileNameWithoutExtension() + " Stems");

    showSeparationProgress();

    stemSeparationEngine.onProgress =
        [safe = juce::Component::SafePointer<MainComponent> (this)] (float p, juce::String stage)
        {
            if (safe != nullptr && safe->separationPanel != nullptr)
                safe->separationPanel->update (p, stage);
        };

    stemSeparationEngine.onError =
        [safe = juce::Component::SafePointer<MainComponent> (this)] (juce::String error)
        {
            if (safe == nullptr) return;
            safe->hideSeparationProgress();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "KI-Stem-Trennung", error);
        };

    stemSeparationEngine.onFinished =
        [safe = juce::Component::SafePointer<MainComponent> (this)]
        (juce::Array<StemSeparationEngine::Stem> stems)
        {
            if (safe == nullptr) return;
            safe->hideSeparationProgress();
            safe->onStemsSeparated (stems);
        };

    if (! stemSeparationEngine.startSeparation (song, outDir, model, 4))
    {
        hideSeparationProgress();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "KI-Stem-Trennung", "Trennung konnte nicht gestartet werden.");
    }
    menuItemsChanged();   // menu item disabled while busy
}

void MainComponent::onStemsSeparated (const juce::Array<StemSeparationEngine::Stem>& stems)
{
    menuItemsChanged();

    // The model KNOWS each stem's instrument — build StemInfos directly, no
    // filename guessing. Silent targets (instrument absent in the song) are
    // skipped and reported instead of polluting the session with dead tracks.
    juce::Array<StemInfo> infos;
    juce::StringArray skipped;

    for (const auto& s : stems)
    {
        if (s.silent)
        {
            skipped.add (s.instrument);
            continue;
        }

        StemInfo info;
        info.file         = s.file;
        info.instrument   = s.instrument;
        info.group        = s.group;
        info.confidence   = 0.95;
        info.fromFilename = false;
        info.peakDb       = s.peakDb;
        info.rmsDb        = s.rmsDb;
        infos.add (info);
    }

    if (! skipped.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "KI-Stem-Trennung",
            juce::String::fromUTF8 ("Nicht im Song gefunden (übersprungen): ")
                + skipped.joinIntoString (", "));

    if (infos.isEmpty())
        return;

    const auto reference = getActiveReferenceProfile();   // REF-01/02 guides staging
    onStemPlanReady (SessionPlanner::plan (infos,
                                           reference.isValid() ? &reference : nullptr));
}

void MainComponent::showSeparationProgress()
{
    if (separationPanel == nullptr)
    {
        separationPanel = std::make_unique<SeparationProgressPanel>();
        addAndMakeVisible (*separationPanel);
    }
    separationPanel->toFront (false);
    resized();
}

void MainComponent::hideSeparationProgress()
{
    separationPanel.reset();
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
    // Aizana scans into its own PluginScanner::knownPlugins (drives the browser), but
    // Tracktion Engine instantiates plugins from engine.getPluginManager().knownPluginList
    // (ExternalPlugin::findMatchingPlugin). Mirror the scanned descriptions across so TE
    // can create real instances — otherwise third-party plugins are inert shells with no
    // editor and no audio. addType() de-dupes by identifier, so repeat calls are safe.
    auto& engineList = appState.editManager.getEngine().getPluginManager().knownPluginList;
    const bool oop = aizana::oopHostingEnabled();
    for (const auto& type : pluginScanner.getKnownPlugins().getTypes())
    {
        // E5.3c: with AIZANA_OOP_PLUGINS=1, route Aizana-generated CLAP plugins through the
        // out-of-process host (rewrite the format name so TE picks OutOfProcessPluginFormat).
        // E5.6: with AIZANA_OOP_ALL=1 additionally route 3rd-party VST3/AU/CLAP.
        // Everything else — and the whole default (flag OFF) path — is unchanged.
        // "Cloudes" = pre-rename vendor baked into old generated .clap binaries (Klasse C).
        const bool generatedClap = (type.manufacturerName == "Aizana" || type.manufacturerName == "Cloudes")
                                    && type.pluginFormatName == "CLAP";
        const bool thirdParty = aizana::oopThirdPartyEnabled()
                                 && (type.pluginFormatName == "VST3"
                                      || type.pluginFormatName == "AudioUnit"
                                      || type.pluginFormatName == "CLAP");

        if (oop && (generatedClap || thirdParty))
        {
            auto oopType = type;
            oopType.pluginFormatName = aizana::kOopFormatName;
            engineList.addType (oopType);
        }
        else
        {
            engineList.addType (type);
        }
    }
}

void MainComponent::pollGeneratedLibrary()
{
    const auto dir = aizana::FaustCompiler::generatedPluginsDir();
    const int count = dir.isDirectory()
                        ? dir.getNumberOfChildFiles (juce::File::findFiles, "*.clap") : 0;

    if (count == lastGeneratedClapCount)   // cheap guard — no dlopen unless the set changed
        return;
    lastGeneratedClapCount = count;

    if (pluginScanner.registerGeneratedPlugins (dir) > 0)
        syncScannedPluginsToEngine();      // a NEW plugin appeared — mirror it to the engine
}

namespace
{
    // DocumentWindow that notifies on close (so MainComponent can drop its handle).
    struct AiStudioWindow : public juce::DocumentWindow
    {
        std::function<void()> onClose;
        AiStudioWindow()
            : juce::DocumentWindow (juce::String::fromUTF8 ("Aizana KI-Studio"),
                                    juce::Colour (0xff1a1a1a),
                                    juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {}
        void closeButtonPressed() override { if (onClose) onClose(); }
    };
}

void MainComponent::openAiStudio()
{
    if (aiStudioWindow != nullptr)
    {
        aiStudioWindow->toFront (true);
        return;
    }

    auto win = std::make_unique<AiStudioWindow>();
    win->setUsingNativeTitleBar (true);
    win->setContentOwned (new AiStudioPanel(), true);
    win->setResizable (true, true);
    win->centreWithSize (480, 560);
    win->setVisible (true);
    win->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->aiStudioWindow.reset(); });
    };

    aiStudioWindow = std::move (win);
}

void MainComponent::openGeneratedPluginsManager()
{
    if (generatedPluginsWindow != nullptr)
    {
        generatedPluginsWindow->toFront (true);
        return;
    }

    // After a rename/delete: re-persist the plugin cache so the change survives restart
    // (a deleted plugin must not resurrect from the XML cache), and refresh the live engine.
    auto onChanged = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;
        safe->pluginScanner.savePluginList (safe->getPluginListCacheFile());
        safe->syncScannedPluginsToEngine();
    };

    auto win = std::make_unique<AiStudioWindow>();   // reuse the close-notifying DocumentWindow
    win->setName (juce::String::fromUTF8 ("Aizana-Plugins verwalten"));
    win->setUsingNativeTitleBar (true);
    win->setContentOwned (new GeneratedPluginsPanel (pluginScanner, std::move (onChanged)), true);
    win->setResizable (true, true);
    win->centreWithSize (460, 480);
    win->setVisible (true);
    win->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->generatedPluginsWindow.reset(); });
    };

    generatedPluginsWindow = std::move (win);
}

juce::File MainComponent::getPluginListCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Aizana")
               .getChildFile ("knownPlugins.xml");
}
