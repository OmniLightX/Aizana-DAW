#include "TrackHeader.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"

//==============================================================================

TrackHeader::TrackHeader (te::AudioTrack&       track_,
                          MixerEngine&          mixerEngine_,
                          PluginManager&        pluginManager_,
                          PluginWindowManager&  pluginWindowManager_,
                          PluginScanner&        pluginScanner_)
    : track             (track_)
    , mixerEngine       (mixerEngine_)
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
}

//==============================================================================

void TrackHeader::resized()
{
    auto area = getLocalBounds().reduced (2);

    // Name label takes full width at top
    trackNameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // Buttons in a row at bottom
    const int btnW = 28;
    muteButton.setBounds (area.removeFromLeft (btnW));
    area.removeFromLeft (2);
    soloButton.setBounds (area.removeFromLeft (btnW));
    area.removeFromLeft (2);
    armButton.setBounds  (area.removeFromLeft (btnW));
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
    if (e.mods.isRightButtonDown())
        showContextMenu();
}

//==============================================================================

void TrackHeader::showContextMenu()
{
    juce::PopupMenu menu;
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
                case 1: handleAddPlugin();          break;
                case 2: handleCreateBus();          break;
                case 3: handleSetSidechainSource(); break;
                case 4: handleRenameTrack();        break;
                case 5: handleDeleteTrack();        break;
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
