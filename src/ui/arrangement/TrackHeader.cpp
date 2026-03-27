#include "TrackHeader.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "plugins/BuiltInPluginFormat.h"

//==============================================================================

TrackHeader::TrackHeader (te::AudioTrack&       track_,
                          MixerEngine&          mixerEngine_,
                          AutomationEngine&     automationEngine_,
                          PluginManager&        pluginManager_,
                          PluginWindowManager&  pluginWindowManager_,
                          PluginScanner&        pluginScanner_)
    : track             (track_)
    , mixerEngine       (mixerEngine_)
    , automationEngine  (automationEngine_)
    , pluginManager     (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner     (pluginScanner_)
{
    // Track name label
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setEditable (false, true, false);  // editable on double-click
    trackNameLabel.setColour (juce::Label::textColourId,       juce::Colours::white);
    trackNameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    trackNameLabel.onTextChange = [this]
    {
        track.setName (trackNameLabel.getText());
    };

    // Style mute/solo/arm buttons
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    armButton.setColour  (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    armButton.setColour  (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc3333));

    muteButton.setClickingTogglesState (true);
    soloButton.setClickingTogglesState (true);
    armButton.setClickingTogglesState  (true);

    // Wire button callbacks
    muteButton.onClick = [this] { muteClicked(); };
    soloButton.onClick = [this] { soloClicked(); };
    armButton.onClick  = [this] { armClicked();  };

    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (armButton);

    // Compact volume slider — horizontal, no text box, wired to MixerEngine
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);  // double-click resets to 0 dB
    volumeSlider.setValue (mixerEngine.getVolume (track), juce::dontSendNotification);
    volumeSlider.setTooltip ("Volume");
    volumeSlider.onValueChange = [this]
    {
        mixerEngine.setVolume (track, (float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    // Pan readout label — shows current pan as L/C/R text, non-interactive
    panLabel.setFont (juce::FontOptions (9.0f));
    panLabel.setColour (juce::Label::textColourId, juce::Colour (0xff999999));
    panLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (panLabel);
    updatePanLabel();

    // Automation mode buttons (R/W/T/L)
    for (auto* btn : { &readButton, &writeButton, &touchButton, &latchButton })
    {
        btn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2a2a2a));
        btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4488ff));
        btn->setClickingTogglesState (false);
        addAndMakeVisible (btn);
    }

    readButton.onClick  = [this] { setAutomationMode (AutomationMode::read);  };
    writeButton.onClick = [this] { setAutomationMode (AutomationMode::write); };
    touchButton.onClick = [this] { setAutomationMode (AutomationMode::touch); };
    latchButton.onClick = [this] { setAutomationMode (AutomationMode::latch); };

    updateModeButtonStates();
}

//==============================================================================

void TrackHeader::resized()
{
    auto area = getLocalBounds().reduced (2);

    // Name label takes full width at top
    trackNameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // Volume slider: horizontal, full width minus pan label, 16px tall, near bottom
    auto controlArea = area.reduced (0, 0);
    auto volumeRow = controlArea.removeFromBottom (16);
    // Pan label: 24px wide on the right, same row as volume slider
    panLabel.setBounds (volumeRow.removeFromRight (24));
    volumeSlider.setBounds (volumeRow);
    controlArea.removeFromBottom (2);

    // Buttons in a row at the remaining area (top of what's left)
    const int btnW = 28;
    auto msrRow = controlArea.removeFromTop (20);
    muteButton.setBounds (msrRow.removeFromLeft (btnW));
    msrRow.removeFromLeft (2);
    soloButton.setBounds (msrRow.removeFromLeft (btnW));
    msrRow.removeFromLeft (2);
    armButton.setBounds  (msrRow.removeFromLeft (btnW));

    // Automation mode row: R/W/T/L below M/S/R row
    controlArea.removeFromTop (2);
    const int kModeRowH = 18;
    auto modeRow = controlArea.removeFromTop (kModeRowH);
    const int modeBtnW = modeRow.getWidth() / 4;
    readButton.setBounds  (modeRow.removeFromLeft (modeBtnW));
    writeButton.setBounds (modeRow.removeFromLeft (modeBtnW));
    touchButton.setBounds (modeRow.removeFromLeft (modeBtnW));
    latchButton.setBounds (modeRow);
}

void TrackHeader::paint (juce::Graphics& g)
{
    // Track colour strip on the left edge (4px wide)
    const auto trackColour = track.getColour();
    g.setColour (trackColour);
    g.fillRect (0, 0, 4, getHeight());

    // Background
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRect (4, 0, getWidth() - 4, getHeight());

    // Selection tint: subtle Apple-style overlay (not garish)
    if (selected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (4, 0, getWidth() - 4, getHeight());
    }

    // Right border separator
    g.setColour (juce::Colour (0xff111111));
    g.drawLine ((float)getWidth() - 1.0f, 0.0f,
                (float)getWidth() - 1.0f, (float)getHeight(), 1.0f);

    // Bottom border
    g.drawLine (0.0f, (float)getHeight() - 1.0f,
                (float)getWidth(), (float)getHeight() - 1.0f, 1.0f);
}

//==============================================================================

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isRightButtonDown())
    {
        // Notify ArrangementView to update selection
        if (onSelectionRequest)
            onSelectionRequest (this);
    }

    if (e.mods.isRightButtonDown())
        showContextMenu();
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    // Only start drag after a minimum drag distance to avoid accidental reorders
    if (e.getDistanceFromDragStart() < 8) return;

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        container->startDragging ("track-reorder",
                                  this,
                                  juce::ScaledImage(),  // no custom drag image
                                  true);                 // allowDraggingToOtherWindows = true
    }
}

void TrackHeader::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

//==============================================================================

void TrackHeader::showContextMenu()
{
    juce::PopupMenu builtInMenu;
    builtInMenu.addItem (10, "EQ (4-Band Parametric)");
    builtInMenu.addItem (11, "Compressor");
    builtInMenu.addItem (12, "Reverb");
    builtInMenu.addItem (13, "Limiter");

    juce::PopupMenu menu;
    menu.addSubMenu ("Built-in FX", builtInMenu);
    menu.addSeparator();
    menu.addItem (1, "Add Plugin...");
    menu.addSeparator();
    menu.addItem (2, "Create Bus from Track");
    menu.addItem (3, "Set Sidechain Source...");
    menu.addSeparator();
    menu.addItem (4, "Rename Track");
    menu.addItem (5, "Delete Track");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result)
        {
            switch (result)
            {
                case 1:  handleAddPlugin();                             break;
                case 2:  handleCreateBus();                            break;
                case 3:  handleSetSidechainSource();                   break;
                case 4:  handleRenameTrack();                          break;
                case 5:  handleDeleteTrack();                          break;
                case 10: handleInsertBuiltInFX ("Cloudes EQ");         break;
                case 11: handleInsertBuiltInFX ("Cloudes Compressor"); break;
                case 12: handleInsertBuiltInFX ("Cloudes Reverb");     break;
                case 13: handleInsertBuiltInFX ("Cloudes Limiter");    break;
                default: break;
            }
        });
}

//==============================================================================

void TrackHeader::handleAddPlugin()
{
    PluginBrowserPopup::showForComponent (
        *this,
        pluginScanner.getKnownPlugins(),
        [this] (const juce::PluginDescription& desc)
        {
            // Insert plugin into this track's chain (appended at end)
            auto* newPlugin = pluginManager.insertPlugin (track, desc);
            if (newPlugin != nullptr)
            {
                // Open the plugin's native editor window immediately.
                // PluginWindowManager handles the MessageManager::callAsync internally.
                pluginWindowManager.openPluginWindow (*newPlugin);
            }
        });
}

void TrackHeader::handleInsertBuiltInFX (const juce::String& fxName)
{
    // Retrieve the pre-built PluginDescription for this built-in FX.
    // BuiltInPluginFormat is registered in PluginScanner's AudioPluginFormatManager,
    // so PluginManager::insertPlugin will route through the standard ExternalPlugin path.
    auto desc = BuiltInPluginFormat::getDescriptionFor (fxName);

    auto* newPlugin = pluginManager.insertPlugin (track, desc);
    if (newPlugin != nullptr)
    {
        // Open the plugin's editor window (shows EQComponent for EQ,
        // GenericAudioProcessorEditor for Compressor/Reverb/Limiter).
        pluginWindowManager.openPluginWindow (*newPlugin);
    }
}

void TrackHeader::handleCreateBus()
{
    // Create a new bus folder track and wire this track to it via an AuxSend at 0 dB.
    const auto busName = track.getName() + " Bus";
    auto* bus = mixerEngine.createBus (busName);
    if (bus != nullptr)
        mixerEngine.createSend (track, *bus, 0.0f);
}

void TrackHeader::handleSetSidechainSource()
{
    // In TE v3.2.0, Track::edit is a reference (not a pointer)
    auto& edit = track.edit;

    // Build a submenu listing all other audio tracks as potential sidechain sources.
    juce::PopupMenu sidechainMenu;
    sidechainMenu.addItem (1, "(None)");
    sidechainMenu.addSeparator();

    // Collect the other tracks so we can map menu result back to track pointer.
    juce::Array<te::AudioTrack*> otherTracks;
    for (auto* t : te::getAudioTracks (edit))
    {
        if (t != &track)
            otherTracks.add (t);
    }

    int id = 2;
    for (auto* t : otherTracks)
        sidechainMenu.addItem (id++, t->getName());

    sidechainMenu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this),
        [this, otherTracks] (int result)
        {
            if (result <= 1)  // (None) or dismissed
                return;

            // result == 2 → otherTracks[0], result == 3 → otherTracks[1], etc.
            const int trackIndex = result - 2;
            if (trackIndex < 0 || trackIndex >= otherTracks.size())
                return;

            auto* sourceTrack = otherTracks[trackIndex];

            // Find the first compressor plugin in this track's chain and sidechain it.
            const auto chain = pluginManager.getPluginChain (track);
            for (auto* plugin : chain)
            {
                if (plugin->getName().containsIgnoreCase ("Compressor"))
                {
                    mixerEngine.setSidechainSource (*plugin, *sourceTrack);
                    break;
                }
            }
        });
}

void TrackHeader::handleRenameTrack()
{
    // Inline edit via the label's built-in editor
    trackNameLabel.showEditor();
}

void TrackHeader::handleDeleteTrack()
{
    // Show confirmation dialog, then delete with undo support
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon,
        "Delete Track",
        "Delete track '" + track.getName() + "'?",
        "Delete",
        "Cancel",
        nullptr,
        juce::ModalCallbackFunction::create ([this] (int result)
        {
            if (result == 1)  // OK pressed
            {
                // In TE v3.2.0, Track::edit is a reference (not a pointer)
                auto& undoManager = track.edit.getUndoManager();
                undoManager.beginNewTransaction ("Delete Track: " + track.getName());
                track.edit.deleteTrack (&track);
            }
        }));
}

//==============================================================================

void TrackHeader::muteClicked()
{
    mixerEngine.setMute (track, muteButton.getToggleState());
}

void TrackHeader::soloClicked()
{
    mixerEngine.setSolo (track, soloButton.getToggleState());
}

void TrackHeader::armClicked()
{
    // Record-arm: store armed state in the track's ValueTree.
    // te::AudioTrack does not have setLivePlayEnabled() in TE v3.2.0 —
    // the arm state is a custom property read by AudioRecorder when starting
    // a recording session.
    const bool armed = armButton.getToggleState();
    track.state.setProperty ("armed", armed, nullptr);
}

//==============================================================================

void TrackHeader::setAutomationMode (AutomationMode mode)
{
    automationEngine.setMode (track, mode);
    updateModeButtonStates();
}

void TrackHeader::updateModeButtonStates()
{
    AutomationMode current = automationEngine.getMode (track);
    readButton.setToggleState  (current == AutomationMode::read,  juce::dontSendNotification);
    writeButton.setToggleState (current == AutomationMode::write, juce::dontSendNotification);
    touchButton.setToggleState (current == AutomationMode::touch, juce::dontSendNotification);
    latchButton.setToggleState (current == AutomationMode::latch, juce::dontSendNotification);
}

//==============================================================================

void TrackHeader::updatePanLabel()
{
    float pan = mixerEngine.getPan (track);
    if (std::abs (pan) < 0.01f)
        panLabel.setText ("C", juce::dontSendNotification);
    else if (pan < 0.0f)
        panLabel.setText ("L" + juce::String ((int)(-pan * 100)), juce::dontSendNotification);
    else
        panLabel.setText ("R" + juce::String ((int)(pan * 100)), juce::dontSendNotification);
}
