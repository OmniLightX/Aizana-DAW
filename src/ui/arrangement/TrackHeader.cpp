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
    // Forward label clicks to TrackHeader::mouseDown so clicking the track name
    // sets selectedTrackHeader + calls grabKeyboardFocus(ArrangementView).
    // Without this, the Label intercepts mouseDown and S/M shortcuts never fire.
    // Same pattern as ChannelStrip (Session 2026-04-13 part 2).
    trackNameLabel.addMouseListener (this, false);

    // Style mute/solo/arm buttons
    muteButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFCC2200));  // red, matches ChannelStrip
    muteButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    soloButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffbbbb00));
    armButton.setColour    (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    armButton.setColour    (juce::TextButton::buttonOnColourId, juce::Colour (0xffcc3333));
    monitorButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    monitorButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2255aa));  // blue = monitoring active
    monitorButton.setTooltip ("Input Monitoring (I)");

    muteButton.setClickingTogglesState    (true);
    soloButton.setClickingTogglesState    (true);
    armButton.setClickingTogglesState     (true);
    monitorButton.setClickingTogglesState (true);

    // Wire button callbacks
    muteButton.onClick    = [this] { muteClicked();    };
    soloButton.onClick    = [this] { soloClicked();    };
    armButton.onClick     = [this] { armClicked();     };
    monitorButton.onClick = [this] { monitorClicked(); };

    // Input selector — Studio One style: small button below M/S/REC/I, click = popup
    inputButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff252525));
    inputButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888888));
    inputButton.setTooltip ("Audio Input — click to change");
    inputButton.onClick = [this] { showInputMenu(); };
    populateInputSelector();

    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (armButton);
    addAndMakeVisible (monitorButton);
    addAndMakeVisible (inputButton);

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

    // Buttons in a row: M / S / REC / I (monitor)
    // Widths: 24 / 24 / 30 / 18 = 96px + 3×2 gaps = 102px — fits any track header ≥110px wide
    auto msrRow = controlArea.removeFromTop (20);
    muteButton.setBounds    (msrRow.removeFromLeft (24));
    msrRow.removeFromLeft (2);
    soloButton.setBounds    (msrRow.removeFromLeft (24));
    msrRow.removeFromLeft (2);
    armButton.setBounds     (msrRow.removeFromLeft (30));
    msrRow.removeFromLeft (2);
    monitorButton.setBounds (msrRow.removeFromLeft (18));

    // Input selector: full width, 14px tall (Studio One style small input label)
    if (controlArea.getHeight() >= 14)
    {
        controlArea.removeFromTop (2);
        inputButton.setBounds (controlArea.removeFromTop (14));
        inputButton.setVisible (true);
    }
    else
    {
        inputButton.setVisible (false);
    }

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

void TrackHeader::refresh()
{
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    soloButton.setToggleState (track.isSolo  (false), juce::dontSendNotification);

    // Sync monitor button: lit when the assigned input is in automatic or on mode
    bool isMonitoring = false;
    for (auto* instance : track.edit.getAllInputDevices())
    {
        if (te::isOnTargetTrack (*instance, track, 0))
        {
            isMonitoring = (instance->getInputDevice().getMonitorMode()
                            != te::InputDevice::MonitorMode::off);
            break;
        }
    }
    monitorButton.setToggleState (isMonitoring, juce::dontSendNotification);
    refreshInputButton();
}

void TrackHeader::toggleMute()
{
    // Direct button state update (no TE read-back) — same pattern as toggleArm().
    // muteButton.getToggleState() reflects the CURRENT visual state before the toggle.
    const bool newMute = ! muteButton.getToggleState();
    muteButton.setToggleState (newMute, juce::dontSendNotification);
    mixerEngine.setMute (track, newMute);
}

void TrackHeader::toggleSolo()
{
    // Direct button state update (no TE read-back) — same pattern as toggleArm().
    const bool newSolo = ! soloButton.getToggleState();
    soloButton.setToggleState (newSolo, juce::dontSendNotification);
    mixerEngine.setSolo (track, newSolo);
}

void TrackHeader::toggleArm()
{
    // Derive the new armed state from the current state property (not button visual),
    // so this is idempotent regardless of visual sync state.
    const bool newArmed = ! (bool) track.state.getProperty ("armed", false);
    armButton.setToggleState (newArmed, juce::dontSendNotification);
    armClicked();  // full TE recording API: assigns WaveInputDevice + setRecordingEnabled
}

//==============================================================================

void TrackHeader::populateInputSelector()
{
    refreshInputButton();  // text is built dynamically in showInputMenu / refreshInputButton
}

void TrackHeader::showInputMenu()
{
    auto& dm = track.edit.engine.getDeviceManager();

    juce::PopupMenu menu;
    menu.addItem (1, "No Input", true, false);
    menu.addSeparator();

    // getWaveInputDevices() calls dispatchPendingUpdates() so the list is
    // always current even if the async device scan hasn't run yet.
    int id = 2;
    for (auto* dev : dm.getWaveInputDevices())
    {
        if (dev == nullptr) continue;

        const juce::String name = dev->getName();
        bool isCurrent = false;
        for (auto* instance : track.edit.getAllInputDevices())
        {
            if (te::isOnTargetTrack (*instance, track, 0)
                && &instance->getInputDevice() == dev)
            {
                isCurrent = true;
                break;
            }
        }
        menu.addItem (id++, name, true, isCurrent);
    }

    // Capture `this` only — re-acquire DeviceManager inside the callback so we
    // never hold a dangling reference to a stack-frame variable across the async gap.
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&inputButton),
        [this] (int result)
        {
            if (result == 1)
            {
                assignInput (nullptr);  // No Input
                return;
            }
            if (result < 2) return;

            auto& dm2 = track.edit.engine.getDeviceManager();
            int id2 = 2;
            bool found = false;
            for (auto* dev : dm2.getWaveInputDevices())
            {
                if (dev == nullptr) continue;
                if (id2 == result)
                {
                    assignInput (dev);
                    found = true;
                    break;
                }
                ++id2;
            }
            if (! found)
                assignInput (nullptr);  // safety fallback = No Input
        });
}

void TrackHeader::assignInput (te::WaveInputDevice* waveIn)
{
    auto& edit = track.edit;

    // Detach any existing assignment for this track
    for (auto* instance : edit.getAllInputDevices())
        if (te::isOnTargetTrack (*instance, track, 0))
            instance->setRecordingEnabled (track.itemID, false);

    if (waveIn == nullptr)
    {
        refreshInputButton();
        return;
    }

    for (auto* instance : edit.getAllInputDevices())
    {
        if (&instance->getInputDevice() == waveIn)
        {
            // CR-03: check result — if setTarget() rejects (device busy / context not ready),
            // bail out rather than showing the device as assigned in the UI.
            auto result = instance->setTarget (track.itemID, true, nullptr);
            if (! result.has_value())
            {
                refreshInputButton();
                return;
            }
            break;
        }
    }

    refreshInputButton();

    // If the track is already armed, re-enable recording on the new device.
    // Without this, opening the input dropdown while armed calls setRecordingEnabled(false)
    // at the top of this function but never restores it — recording silently stays disabled.
    if ((bool) track.state.getProperty ("armed", false))
    {
        for (auto* instance : edit.getAllInputDevices())
            if (&instance->getInputDevice() == waveIn)
                instance->setRecordingEnabled (track.itemID, true);

        // WR-02: setTarget() fires triggerAsyncUpdate() which may reset recording-enabled state.
        // Schedule a re-apply after the async update settles, same as the ARM path.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<TrackHeader> (this), waveIn]()
        {
            if (safeThis == nullptr) return;
            if (! (bool) safeThis->track.state.getProperty ("armed", false)) return;
            for (auto* inst : safeThis->track.edit.getAllInputDevices())
                if (&inst->getInputDevice() == waveIn && te::isOnTargetTrack (*inst, safeThis->track, 0))
                    inst->setRecordingEnabled (safeThis->track.itemID, true);
        });
    }
}

void TrackHeader::refreshInputButton()
{
    auto& edit = track.edit;
    juce::String inputName = "No Input";

    for (auto* instance : edit.getAllInputDevices())
    {
        if (te::isOnTargetTrack (*instance, track, 0))
        {
            inputName = instance->getInputDevice().getName();
            break;
        }
    }

    // Truncate long device names to fit the narrow header
    if (inputName.length() > 18)
        inputName = inputName.substring (0, 17) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")); // "…"

    inputButton.setButtonText (inputName);
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
    menu.addItem (6, "Create FX Channel");
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
                case 6:  handleCreateFXChannel();                      break;
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
    // Create a new Bus Channel and redirect this track's main output into it.
    // routeTrackToBus() calls setOutputToTrack() — serial routing through the bus
    // fader to master. createSend() would instead create a parallel AuxSend (FX-style),
    // which is wrong for a bus channel and causes the bus fader to have no effect.
    const auto busName = track.getName() + " Bus";
    auto* bus = mixerEngine.createBus (busName);
    if (bus != nullptr)
        mixerEngine.routeTrackToBus (track, *bus);
}

void TrackHeader::handleCreateFXChannel()
{
    // Create a new FX channel (AuxReturn) and add an AuxSend on this track targeting it.
    const auto fxName = track.getName() + " FX";
    auto* fxChannel = mixerEngine.createFXChannel (fxName);
    if (fxChannel != nullptr)
        mixerEngine.createFXSend (track, *fxChannel, -6.0f);
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
    // Clicking any button implicitly selects the track (Studio One behaviour),
    // so subsequent S/M keyboard shortcuts know which track to target.
    if (onSelectionRequest) onSelectionRequest (this);
    mixerEngine.setMute (track, muteButton.getToggleState());
}

void TrackHeader::soloClicked()
{
    if (onSelectionRequest) onSelectionRequest (this);
    mixerEngine.setSolo (track, soloButton.getToggleState());
}

void TrackHeader::armClicked()
{
    const bool armed = armButton.getToggleState();
    track.state.setProperty ("armed", armed, nullptr);

    auto& edit = track.edit;

    if (armed)
    {
        // TE records only from tracks that have an InputDeviceInstance assigned and
        // recording-enabled. Check if one is already assigned.
        bool alreadyAssigned = false;
        for (auto* instance : edit.getAllInputDevices())
        {
            if (te::isOnTargetTrack (*instance, track, 0))
            {
                alreadyAssigned = true;
                instance->setRecordingEnabled (track.itemID, true);
                break;
            }
        }

        // No input assigned yet — auto-assign the first enabled wave (audio) input.
        // This mirrors Studio One: pressing REC on a track silently picks the default mic.
        if (! alreadyAssigned)
        {
            // getWaveInputDevices() calls dispatchPendingUpdates() internally,
            // guaranteeing the device list is populated even on first ARM click.
            auto& dm = edit.engine.getDeviceManager();
            for (auto* waveIn : dm.getWaveInputDevices())
            {
                if (waveIn == nullptr)
                    continue;

                for (auto* instance : edit.getAllInputDevices())
                {
                    if (&instance->getInputDevice() == waveIn)
                    {
                        auto assignResult = instance->setTarget (track.itemID, true, nullptr);
                        if (assignResult.has_value())
                            instance->setRecordingEnabled (track.itemID, true);
                        break;
                    }
                }
                break;  // only first available device
            }
        }

        // ARM automatically enables monitoring (Studio One / Logic behaviour).
        // Set the I button now; the callAsync below applies the mode after setTarget()'s
        // async update has settled (it resets MonitorMode to off on !alreadyAssigned path).
        monitorButton.setToggleState (true, juce::dontSendNotification);
        refreshInputButton();

        // setTarget() calls triggerAsyncUpdate() internally — the Destination may be
        // recreated on the next message loop tick, wiping the recording-enabled flag and
        // MonitorMode we just set.  Re-apply both after the async update settles.
        // WR-01: respect the current I-button state rather than hardcoding 'automatic',
        // so a user who clicks I-off immediately after ARM isn't overridden.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<TrackHeader> (this)]()
        {
            if (safeThis == nullptr) return;
            if (! (bool) safeThis->track.state.getProperty ("armed", false)) return;
            const bool wantMonitor = safeThis->monitorButton.getToggleState();
            for (auto* inst : safeThis->track.edit.getAllInputDevices())
            {
                if (te::isOnTargetTrack (*inst, safeThis->track, 0))
                {
                    inst->setRecordingEnabled (safeThis->track.itemID, true);
                    inst->getInputDevice().setMonitorMode (
                        wantMonitor ? te::InputDevice::MonitorMode::automatic
                                    : te::InputDevice::MonitorMode::off);
                }
            }
        });
    }
    else
    {
        // Disarm — disable recording and monitoring on all instances targeting this track.
        for (auto* instance : edit.getAllInputDevices())
        {
            if (te::isOnTargetTrack (*instance, track, 0))
            {
                instance->setRecordingEnabled (track.itemID, false);
                instance->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::off);
            }
        }
        monitorButton.setToggleState (false, juce::dontSendNotification);
    }
}

void TrackHeader::monitorClicked()
{
    const bool monitorOn = monitorButton.getToggleState();
    auto& edit = track.edit;

    // Find the input device assigned to this track and set its monitor mode.
    // MonitorMode::automatic = monitor only while recording (Studio One default).
    // MonitorMode::off = no monitoring.
    for (auto* instance : edit.getAllInputDevices())
    {
        if (te::isOnTargetTrack (*instance, track, 0))
        {
            auto newMode = monitorOn
                ? te::InputDevice::MonitorMode::automatic
                : te::InputDevice::MonitorMode::off;
            instance->getInputDevice().setMonitorMode (newMode);
            break;
        }
    }
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
