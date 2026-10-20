#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/ExportPipeline.h"
#include "engine/EditManager.h"

class ExportDialog : public juce::Component
{
public:
    enum class Mode { Mixdown, Stems };

    ExportDialog (Mode mode, EditManager& editManager, ExportPipeline& exportPipeline);

    // Convenience static methods to show as modal dialog window
    static void showMixdownDialog (EditManager& editManager,
                                   ExportPipeline& exportPipeline);
    static void showStemsDialog (EditManager& editManager,
                                 ExportPipeline& exportPipeline);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    Mode mode;
    EditManager& editManager;
    ExportPipeline& exportPipeline;

    // Format controls
    juce::ComboBox formatCombo;       // WAV / AIFF / MP3
    juce::ComboBox sampleRateCombo;   // 44100 / 48000 / 96000
    juce::ComboBox bitDepthCombo;     // 16 / 24 / 32

    // Output path
    juce::TextEditor outputPathEditor;
    juce::TextButton browseButton { "Auswahl..." };

    // Progress — ProgressBar requires a reference to a double in its constructor
    double exportProgress = 0.0;     // 0.0 to 1.0, referenced by ProgressBar
    juce::ProgressBar progressBar { exportProgress };

    // Action buttons
    juce::TextButton exportButton { "Export" };
    juce::TextButton cancelButton { "Abbrechen" };

    juce::Label statusLabel;  // Shows "Exporting...", "Done!", or error message

    bool isExporting = false;
    bool cancelRequested = false;
    juce::Label formatLabel, rateLabel, qualityLabel;
    void updateFormat();

    // Async file chooser — stored as member to keep alive across async callbacks
    std::unique_ptr<juce::FileChooser> fileChooser;

    void browse();
    void startExport(bool overwriteConfirmed = false);
    ExportOptions buildOptions() const;
};
