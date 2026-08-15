#include "PreferencesDialog.h"

//==============================================================================

PreferencesDialog::PreferencesDialog (ApplicationState& appState_)
    : appState (appState_)
{
    // ---- Section label ----
    midiClockSectionLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    midiClockSectionLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (midiClockSectionLabel);

    // ---- MIDI Clock mode combo ----
    // ---- Autosave-Intervall (User-Wunsch 2026-08-15) ----
    // Fest verdrahtete zwei Minuten schreiben ueber einen langen Arbeitstag
    // unnoetig oft auf die Platte. Jetzt waehlbar — inklusive "aus".
    {
        int id = 1;
        for (int m : EditManager::kAutosaveMinutesChoices)
        {
            autosaveCombo.addItem (m == 0 ? juce::String::fromUTF8 ("aus")
                                          : juce::String (m) + " Minuten", id++);
        }

        const int current = appState.editManager.getAutosaveIntervalMinutes();
        int sel = 1, i = 1;
        for (int m : EditManager::kAutosaveMinutesChoices)
        {
            if (m == current) sel = i;
            ++i;
        }
        autosaveCombo.setSelectedId (sel, juce::dontSendNotification);
        autosaveCombo.setTitle ("Autosave Intervall");
        autosaveCombo.onChange = [this] { autosaveIntervalChanged(); };
    }

    midiClockModeCombo.addItem ("Off",    1);
    midiClockModeCombo.addItem ("Master", 2);
    midiClockModeCombo.addItem ("Slave",  3);
    midiClockModeCombo.setSelectedId (1, juce::dontSendNotification);  // default Off
    midiClockModeCombo.onChange = [this] { midiClockModeChanged(); };
    addAndMakeVisible (midiClockModeLabel);
    addAndMakeVisible (midiClockModeCombo);

    // ---- Informational note ----
    midiClockNoteLabel.setFont (juce::FontOptions (10.0f));
    midiClockNoteLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    midiClockNoteLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (midiClockNoteLabel);

    autosaveSectionLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    autosaveSectionLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    autosaveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    autosaveNoteLabel.setFont (juce::FontOptions (10.0f));
    autosaveNoteLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    autosaveNoteLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (autosaveSectionLabel);
    addAndMakeVisible (autosaveLabel);
    addAndMakeVisible (autosaveCombo);
    addAndMakeVisible (autosaveNoteLabel);
}

void PreferencesDialog::resized()
{
    auto area = getLocalBounds().reduced (16);

    // MIDI Clock section
    midiClockSectionLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto modeRow = area.removeFromTop (28);
    midiClockModeLabel.setBounds (modeRow.removeFromLeft (80));
    midiClockModeCombo.setBounds (modeRow.removeFromLeft (120));

    area.removeFromTop (8);
    midiClockNoteLabel.setBounds (area.removeFromTop (50));

    // Autosave section
    area.removeFromTop (12);
    autosaveSectionLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);

    auto autoRow = area.removeFromTop (28);
    autosaveLabel.setBounds (autoRow.removeFromLeft (80));
    autosaveCombo.setBounds (autoRow.removeFromLeft (140));

    area.removeFromTop (8);
    autosaveNoteLabel.setBounds (area.removeFromTop (44));
}

void PreferencesDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2a2a));

    // Divider below the section title
    const auto titleBottom = midiClockSectionLabel.getBottom() + 2;
    g.setColour (juce::Colour (0xff444444));
    g.drawHorizontalLine (titleBottom, 16.0f, (float)getWidth() - 16.0f);
}

void PreferencesDialog::midiClockModeChanged()
{
    const int selectedId = midiClockModeCombo.getSelectedId();

    // Store the preference in JUCE's ApplicationProperties if needed in Phase 2.
    // For Phase 1, log to the TE engine debug channel.
    if (selectedId == 2)
    {
        // Master mode — Tracktion Engine will broadcast MIDI clock from its transport
        // via te::Engine::getDeviceManager() when full MIDI device management is wired
        // in Phase 2. For Phase 1 this records user intent.
        DBG ("MIDI Clock: Master mode selected");
    }
    else if (selectedId == 3)
    {
        DBG ("MIDI Clock: Slave mode selected");
    }
    else
    {
        DBG ("MIDI Clock: Off");
    }
}

//==============================================================================

void PreferencesDialog::show (ApplicationState& appState)
{
    auto* dialog = new PreferencesDialog (appState);
    dialog->setSize (360, 340);

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle             = "Preferences";
    opts.dialogBackgroundColour  = juce::Colour (0xff2a2a2a);
    opts.content.setOwned (dialog);
    opts.useNativeTitleBar       = false;
    opts.resizable               = false;
    opts.launchAsync();
}


void PreferencesDialog::autosaveIntervalChanged()
{
    const int index = autosaveCombo.getSelectedId() - 1;
    const auto& choices = EditManager::kAutosaveMinutesChoices;

    if (index >= 0 && index < (int) (sizeof (choices) / sizeof (choices[0])))
        appState.editManager.setAutosaveIntervalMinutes (choices[index]);
}
