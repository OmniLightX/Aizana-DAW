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

    // Allow this component to receive key events
    setWantsKeyboardFocus (true);

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

    if (mixerVisible)
    {
        // MixerView takes bottom ~35% of remaining area
        const int mixerHeight = (int)(area.getHeight() * 0.35f);
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

    return false;
}

void MainComponent::toggleMixer()
{
    mixerVisible = !mixerVisible;
    mixerView.setVisible (mixerVisible);
    resized();   // Re-layout so ArrangementView expands/collapses accordingly
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
