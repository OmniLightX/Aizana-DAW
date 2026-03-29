#include "MainComponent.h"

namespace te = tracktion::engine;

//==============================================================================

MainComponent::MainComponent (ApplicationState& appState_)
    : appState (appState_)
{
    addAndMakeVisible (transportBar);
    addAndMakeVisible (arrangementView);
    addAndMakeVisible (mixerView);

    // DockedPluginPanel from PluginWindowManager — right-side panel
    addAndMakeVisible (pluginWindowManager.getDockedPanel());

    // Mixer visible by default (CONTEXT.md: mixing-first design)
    mixerView.setVisible (mixerVisible);

    // Load previously-scanned plugin list so the browser is populated on startup.
    // If no cache exists (first launch), auto-scan immediately so the browser populates.
    pluginScanner.loadPluginList (getPluginListCacheFile());

    // Auto-scan on first launch when no cached plugins are found
    if (pluginScanner.getKnownPlugins().getNumTypes() == 0)
    {
        juce::MessageManager::callAsync ([this]
        {
            handleScanPlugins();
        });
    }

    // Allow this component to receive key events
    setWantsKeyboardFocus (true);

    addChildComponent (pianoRollCloseButton);  // hidden by default
    pianoRollCloseButton.onClick = [this] { hidePianoRoll(); };

    setSize (1280, 800);
}

void MainComponent::resized()
{
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

    if (mixerVisible)
    {
        // MixerView takes bottom ~50% of remaining area (ChannelStrip needs ~276px minimum)
        const int mixerHeight = (int)(area.getHeight() * 0.50f);
        mixerView.setBounds (area.removeFromBottom (mixerHeight));
    }

    // ArrangementView takes remaining upper area
    arrangementView.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Cmd+, (macOS) / Ctrl+, (Windows) opens Preferences (MIDI clock, audio settings)
    if (key == juce::KeyPress (',', juce::ModifierKeys::commandModifier, 0) ||
        key == juce::KeyPress (',', juce::ModifierKeys::ctrlModifier, 0))
    {
        PreferencesDialog::show (appState);
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

    // Cmd+C / Cmd+D / Cmd+V — forward to ArrangementView so they work regardless
    // of which component currently holds keyboard focus.
    if (key.getModifiers().isCommandDown())
    {
        const int k = key.getKeyCode();
        if (k == 'c' || k == 'C' || k == 'd' || k == 'D' || k == 'v' || k == 'V')
            return arrangementView.keyPressed (key);
    }

    // Spacebar: global play/stop — works from anywhere in the app (no focus trap)
    if (key == juce::KeyPress::spaceKey)
    {
        if (auto* edit = appState.editManager.getEdit())
        {
            auto& transport = edit->getTransport();
            if (transport.isPlaying())
                transport.stop (false, false);
            else
                transport.play (false);
        }
        return true;
    }

    return false;
}

void MainComponent::toggleMixer()
{
    mixerVisible = !mixerVisible;
    mixerView.setVisible (mixerVisible);
    resized();   // Re-layout so ArrangementView expands/collapses accordingly
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
    return { "File", "Edit", "View", "Export" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String& /*name*/)
{
    juce::PopupMenu menu;
    auto* edit = appState.editManager.getEdit();
    const bool hasEdit = (edit != nullptr);

    switch (index)
    {
        case 0:  // File
            menu.addItem (FileNewSession,  "New Session",     true);
            menu.addItem (FileOpen,        "Open Session...", true);
            menu.addSeparator();
            menu.addItem (FileSave,        "Save",            hasEdit);
            menu.addItem (FileSaveAs,      "Save As...",      hasEdit);
            menu.addSeparator();
            menu.addItem (FileImportAudio, "Import Audio...", hasEdit);
            break;

        case 1:  // Edit
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

        case 2:  // View
            menu.addItem (ViewToggleMixer,
                          mixerVisible ? "Hide Mixer" : "Show Mixer",
                          true,
                          mixerVisible);
            menu.addSeparator();
            menu.addItem (ScanPlugins, "Scan Plugins...", true);
            break;

        case 3:  // Export
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
                waveClip->getSourceFileReference().setToDirectFileReference (audioFile, true);

            fileChooser.reset();
        });
}

//==============================================================================
// View menu handlers
//==============================================================================

void MainComponent::handleScanPlugins()
{
    // Show a non-blocking notification while scanning.
    // Apple approach: minimal feedback — transient informational alert,
    // not a modal dialog with a progress bar.
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        "Scanning Plugins",
        "Scanning plugin directories in the background.\n"
        "The plugin browser will update when the scan completes.",
        "OK");

    // scanForPlugins now launches its own background thread and returns immediately.
    // onComplete is called from the background thread — UI updates use callAsync.
    pluginScanner.scanForPlugins (
        [] (float /*progress*/) {},   // progress callback (no progress bar in Phase 1)
        [this] ()
        {
            // Save results to cache so the next launch loads immediately.
            // Called on background thread — savePluginList is file I/O, safe here.
            pluginScanner.savePluginList (getPluginListCacheFile());

            // Post UI notification back to the message thread
            juce::MessageManager::callAsync ([this]
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "Scan Complete",
                    "Plugin scan finished. The browser now shows all found plugins.",
                    "OK");
            });
        });
}

juce::File MainComponent::getPluginListCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Cloudes")
               .getChildFile ("knownPlugins.xml");
}
