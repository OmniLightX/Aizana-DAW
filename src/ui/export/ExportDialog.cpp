#include "ui/export/ExportDialog.h"

ExportDialog::ExportDialog (Mode mode_, EditManager& editManager_,
                            ExportPipeline& exportPipeline_)
    : mode (mode_), editManager (editManager_), exportPipeline (exportPipeline_)
{
    // Format combo (EXP-01, EXP-04)
    formatCombo.addItem ("WAV",   1);
    formatCombo.addItem ("AIFF",  2);
    formatCombo.addItem ("MP3",   3);
    formatCombo.setSelectedId (1);  // WAV default
    addAndMakeVisible (formatCombo);

    // Sample rate combo (EXP-04)
    sampleRateCombo.addItem ("44.1 kHz",  1);
    sampleRateCombo.addItem ("48 kHz",    2);
    sampleRateCombo.addItem ("96 kHz",    3);
    sampleRateCombo.setSelectedId (1);  // 44.1 kHz default
    addAndMakeVisible (sampleRateCombo);

    // Bit depth combo (EXP-04)
    bitDepthCombo.addItem ("16-bit",  1);
    bitDepthCombo.addItem ("24-bit",  2);
    bitDepthCombo.addItem ("32-bit",  3);
    bitDepthCombo.setSelectedId (2);  // 24-bit default
    addAndMakeVisible (bitDepthCombo);

    addAndMakeVisible (outputPathEditor);
    outputPathEditor.setReadOnly (true);
    outputPathEditor.setText (
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
            .getChildFile (mode == Mode::Mixdown ? "mixdown.wav" : "stems")
            .getFullPathName());

    addAndMakeVisible (browseButton);
    browseButton.onClick = [this] { browse(); };

    addAndMakeVisible (progressBar);

    addAndMakeVisible (exportButton);
    exportButton.onClick = [this] { startExport(); };

    addAndMakeVisible (cancelButton);
    cancelButton.onClick = [this]
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    };

    addAndMakeVisible (statusLabel);

    setSize (420, 260);
}

void ExportDialog::showMixdownDialog (EditManager& em, ExportPipeline& ep)
{
    auto* dialog = new ExportDialog (Mode::Mixdown, em, ep);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle = "Export Mixdown";
    opts.dialogBackgroundColour = juce::Colours::darkgrey;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void ExportDialog::showStemsDialog (EditManager& em, ExportPipeline& ep)
{
    auto* dialog = new ExportDialog (Mode::Stems, em, ep);
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (dialog);
    opts.dialogTitle = "Export Stems";
    opts.dialogBackgroundColour = juce::Colours::darkgrey;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

ExportOptions ExportDialog::buildOptions() const
{
    ExportOptions opts;

    // Format
    switch (formatCombo.getSelectedId())
    {
        case 1:  opts.format = ExportFormat::WAV;  break;
        case 2:  opts.format = ExportFormat::AIFF; break;
        case 3:  opts.format = ExportFormat::MP3;  break;
        default: opts.format = ExportFormat::WAV;
    }

    // Sample rate
    switch (sampleRateCombo.getSelectedId())
    {
        case 1:  opts.sampleRate = 44100.0; break;
        case 2:  opts.sampleRate = 48000.0; break;
        case 3:  opts.sampleRate = 96000.0; break;
        default: opts.sampleRate = 44100.0;
    }

    // Bit depth
    switch (bitDepthCombo.getSelectedId())
    {
        case 1:  opts.bitDepth = 16; break;
        case 2:  opts.bitDepth = 24; break;
        case 3:  opts.bitDepth = 32; break;
        default: opts.bitDepth = 24;
    }

    opts.outputFile = juce::File (outputPathEditor.getText());
    return opts;
}

void ExportDialog::browse()
{
    // JUCE 8: synchronous FileChooser API is not available (JUCE_MODAL_LOOPS_PERMITTED not set)
    // Use launchAsync with member fileChooser to keep the chooser alive across the callback
    if (mode == Mode::Mixdown)
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose export location",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
            "*.wav;*.aiff;*.mp3");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto result = fc.getResult();
                if (result != juce::File{})
                    outputPathEditor.setText (result.getFullPathName());
                fileChooser.reset();
            });
    }
    else  // Stems: choose output directory
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose stems output folder",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                const auto result = fc.getResult();
                if (result != juce::File{})
                    outputPathEditor.setText (result.getFullPathName());
                fileChooser.reset();
            });
    }
}

void ExportDialog::startExport()
{
    if (isExporting) return;

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
    {
        statusLabel.setText ("No session open.", juce::dontSendNotification);
        return;
    }

    isExporting = true;
    exportButton.setEnabled (false);
    statusLabel.setText ("Exporting...", juce::dontSendNotification);

    auto options = buildOptions();

    // Wire progress callback — arrives on background thread, must post to message thread
    exportPipeline.onProgress = [this] (float fraction)
    {
        juce::MessageManager::callAsync ([this, fraction]
        {
            exportProgress = fraction;
            progressBar.repaint();
        });
    };

    // Run export on a background thread — exportMixdown() is blocking
    juce::Thread::launch ([this, edit, options]
    {
        juce::String errorMsg;
        if (mode == Mode::Mixdown)
            errorMsg = exportPipeline.exportMixdown (*edit, options);
        else
            errorMsg = exportPipeline.exportStems (*edit, options);

        juce::MessageManager::callAsync ([this, errorMsg]
        {
            isExporting = false;
            exportButton.setEnabled (true);
            exportProgress = 1.0;

            if (errorMsg.isEmpty())
            {
                statusLabel.setText ("Export complete!", juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "Export Complete",
                    "File exported successfully.");
            }
            else
            {
                statusLabel.setText ("Error: " + errorMsg, juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Export Failed",
                    errorMsg);
            }
        });
    });
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void ExportDialog::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Row 1: Format
    auto row = area.removeFromTop (30);
    row.removeFromLeft (80);  // label space
    formatCombo.setBounds (row);
    area.removeFromTop (6);

    // Row 2: Sample rate
    row = area.removeFromTop (30);
    row.removeFromLeft (80);
    sampleRateCombo.setBounds (row);
    area.removeFromTop (6);

    // Row 3: Bit depth
    row = area.removeFromTop (30);
    row.removeFromLeft (80);
    bitDepthCombo.setBounds (row);
    area.removeFromTop (6);

    // Row 4: Output path + browse button
    row = area.removeFromTop (30);
    browseButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (4);
    outputPathEditor.setBounds (row);
    area.removeFromTop (6);

    // Row 5: Progress bar
    progressBar.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    // Row 6: Status label
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    // Row 7: Buttons
    row = area.removeFromTop (30);
    cancelButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (8);
    exportButton.setBounds (row.removeFromRight (80));
}
