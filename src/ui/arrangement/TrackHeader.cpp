#include "TrackHeader.h"
#include "engine/InstrumentTracks.h"
#include "engine/ClipEditor.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "ui/audiosetup/AudioIOSetupDialog.h"
#include "ui/common/TrackColours.h"
#include "plugins/BuiltInPluginFormat.h"
#include "engine/TrackFreeze.h"

// Track channel-format flag (mono/stereo). Persisted on the track ValueTree and
// fully decoupled from hardware input pairing — see TrackHeader::toggleTrackStereo().
static const juce::Identifier kTrackStereoProp { "aizanaTrackStereo" };

//==============================================================================

TrackHeader::TrackHeader (te::AudioTrack&       track_,
                          MixerEngine&          mixerEngine_,
                          AutomationEngine&     automationEngine_,
                          PluginManager&        pluginManager_,
                          PluginWindowManager&  pluginWindowManager_,
                          PluginScanner&        pluginScanner_,
                          InputBusManager&      inputBusManager_)
    : track             (track_)
    , mixerEngine       (mixerEngine_)
    , automationEngine  (automationEngine_)
    , pluginManager     (pluginManager_)
    , pluginWindowManager (pluginWindowManager_)
    , pluginScanner     (pluginScanner_)
    , inputBusManager   (inputBusManager_)
{
    // Track name label
    trackNameLabel.setText (track.getName(), juce::dontSendNotification);
    trackNameLabel.setEditable (false, true, false);  // editable on double-click
    trackNameLabel.setColour (juce::Label::textColourId,       juce::Colours::white);
    trackNameLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    trackNameLabel.onTextChange = [this]
    {
        track.setName (trackNameLabel.getText());

        // Die Namen der Bedienelemente sind mit dem SPURNAMEN qualifiziert ("Stumm
        // Gitarre"). Ohne diesen Aufruf bleiben sie nach dem Umbenennen auf dem ALTEN
        // Namen stehen — VoiceOver sagt dann den falschen an, und jede Adressierung
        // ueber den Namen greift ins Leere. Gefunden beim GUI-Nachweis D4 (2026-08-12).
        updateAccessibleNames();
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

    // I-04: setClickingTogglesState(false) — JUCE must never auto-toggle; we drive state explicitly
    muteButton.setClickingTogglesState    (false);
    soloButton.setClickingTogglesState    (false);
    armButton.setClickingTogglesState     (false);
    monitorButton.setClickingTogglesState (false);

    // Wire button callbacks — explicit toggle before handler so getToggleState() reads new value
    muteButton.onClick    = [this] { muteButton.setToggleState    (!muteButton.getToggleState(),    juce::dontSendNotification); muteClicked();    };
    soloButton.onClick    = [this] { soloButton.setToggleState    (!soloButton.getToggleState(),    juce::dontSendNotification); soloClicked();    };
    armButton.onClick     = [this] { toggleArm(); };
    monitorButton.onClick = [this] { monitorButton.setToggleState (!monitorButton.getToggleState(), juce::dontSendNotification); monitorClicked(); };

    // Input selector — Studio One style: small button below M/S/REC/I, click = popup
    inputButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff252525));
    inputButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888888));
    inputButton.setTooltip (juce::String::fromUTF8 ("Audioeingang wählen"));
    inputButton.onClick = [this] { showInputMenu(); };
    populateInputSelector();

    // Mono/Stereo toggle — Studio One one-click input channel-format switch.
    stereoButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff252525));
    stereoButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2d6a4f));  // green when stereo
    stereoButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff888888));
    stereoButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    stereoButton.setClickingTogglesState (false);  // I-04: drive state explicitly
    stereoButton.setTooltip (juce::String::fromUTF8 ("Spurformat Mono/Stereo umschalten (Hardwareeingang bleibt unverändert)"));
    stereoButton.onClick = [this] { toggleTrackStereo(); };

    colourBar.onClick = [this] { showTrackColourMenu(); };
    addAndMakeVisible (colourBar);
    addAndMakeVisible (trackNameLabel);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (armButton);
    addAndMakeVisible (monitorButton);
    addAndMakeVisible (inputButton);
    addAndMakeVisible (stereoButton);
    refreshStereoButton();

    // Compact volume slider — horizontal, no text box, wired to MixerEngine
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    // dB readout next to the fader (Studio One style — user request 2026-06-13:
    // the arrangement had no dB value, only the mixer). Editable: click to type.
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 14);
    volumeSlider.setNumDecimalPlacesToDisplay (1);
    volumeSlider.setTextValueSuffix (" dB");
    volumeSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffb6bcc4));
    volumeSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
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

    // Keep M/S/volume in sync with changes from ANY source (mixer strip, VCA,
    // AI batch, undo) — the listener receives the whole track subtree.
    track.state.addListener (this);

    // Live level meter behind the dB scale — same source as the mixer strip.
    levelSubscription = std::make_unique<aizana::TrackMeterSubscription> (track);
    startTimerHz (30);

    // Zustand UND Namen sofort aus der Spur holen.
    //
    // Hier stand vorher nur `updateAccessibleNames()`, mit der Begruendung
    // „refresh() laeuft beim Aufbau nicht zwingend". Genau das war der Fehler:
    // ein frisch gebauter Spurkopf zeigte M und S IMMER aus, egal was in der
    // Spur stand. Beim Laden einer Session werden alle Spurkoepfe neu gebaut —
    // also sahen sieben gemutete Spuren nach dem Oeffnen unmuted aus, obwohl
    // die Engine sie korrekt stumm hielt (User-Befund 2026-08-18, an der echten
    // Session nachgemessen: mute="1" in der Datei, m=1 in der Engine, Knopf aus).
    refresh();
}

TrackHeader::~TrackHeader()
{
    stopTimer();
    track.state.removeListener (this);
    levelSubscription.reset();
}

void TrackHeader::timerCallback()
{
    const auto levelL = levelSubscription->client.getAndClearAudioLevel (0);
    const auto levelR = levelSubscription->client.getAndClearAudioLevel (1);
    const float peak  = juce::jmax (levelL.dB, levelR.dB);

    // Rise instantly, fall smoothly (ballistics like the mixer meter)
    const float next = juce::jmax (peak, meterDb - 2.0f);
    if (std::abs (next - meterDb) > 0.1f)
    {
        meterDb = next;
        repaint (dbScaleArea);
    }
    else
    {
        meterDb = next;
    }

    // ---- Fader/dB folgen der Automation beim Playback (S1/Logic-Verhalten).
    // Automations-Playback setzt nur den Parameter-currentValue, nie das
    // ValueTree — der te::IDs::volume-Listener sieht davon nichts.
    // Reiner Anzeige-Sync: dontSendNotification + Drag-Guard, nie zurückschreiben.
    if (! volumeSlider.isMouseButtonDown()
        && track.edit.getTransport().isPlaying())
    {
        const float liveDb = mixerEngine.getLiveVolumeDb (track);
        if (std::abs ((float) volumeSlider.getValue() - liveDb) > 0.05f)
            volumeSlider.setValue (liveDb, juce::dontSendNotification);
    }
}

void TrackHeader::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& id)
{
    if (id == te::IDs::mute || id == te::IDs::solo || id == juce::Identifier ("armed"))
    {
        refresh();
        return;
    }

    // Freeze state changed (also fires when undone/redone or set elsewhere) →
    // redraw so the frozen indicator appears/disappears.
    if (id == te::IDs::frozenIndividually)
    {
        repaint();
        return;
    }

    // Volume lives on the VolumeAndPanPlugin child state. Don't fight the user
    // mid-drag — the slider is the source of truth while the mouse is down.
    if (id == te::IDs::volume && ! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (mixerEngine.getVolume (track), juce::dontSendNotification);

    if (id == te::IDs::pan)
        updatePanLabel();
}

//==============================================================================

void TrackHeader::resized()
{
    auto full = getLocalBounds();

    // Dedicated colour bar on the left edge (clickable colour picker, Studio One style)
    colourBar.setBounds (full.removeFromLeft (6));

    auto area = full.reduced (2);

    // Name label takes full width at top
    trackNameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // Volume slider: horizontal, full width minus pan label, 16px tall, near
    // bottom — with a dB tick scale beneath it (user request 2026-06-13: the
    // mixer has a scale, the arrangement only had the number).
    auto controlArea = area.reduced (0, 0);
    dbScaleArea = controlArea.removeFromBottom (9);
    auto volumeRow = controlArea.removeFromBottom (14);
    // UX1: Pan-Label 34px — "L100"/"R100" wurden bei 24px abgeschnitten.
    panLabel.setBounds (volumeRow.removeFromRight (34));
    volumeSlider.setBounds (volumeRow);
    controlArea.removeFromBottom (1);

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

    // Input selector + Mono/Stereo toggle share one 14px row (Studio One style).
    if (controlArea.getHeight() >= 14)
    {
        controlArea.removeFromTop (2);
        auto inputRow = controlArea.removeFromTop (14);

        // Mono/Stereo toggle on the right — audio tracks only.
        if (! isMidiTrack())
        {
            stereoButton.setBounds (inputRow.removeFromRight (28));
            inputRow.removeFromRight (2);
            stereoButton.setVisible (true);
        }
        else
        {
            stereoButton.setVisible (false);
        }

        inputButton.setBounds (inputRow);
        inputButton.setVisible (true);
    }
    else
    {
        inputButton.setVisible (false);
        stereoButton.setVisible (false);
    }

}

void TrackHeader::paint (juce::Graphics& g)
{
    // The track colour bar (left 6px) is drawn by the ColourBar child component.

    // Background (right of the colour bar)
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRect (6, 0, getWidth() - 6, getHeight());

    // Selection tint: subtle Apple-style overlay (not garish)
    if (selected)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (6, 0, getWidth() - 6, getHeight());
    }

    // Frozen indicator: icy wash + snowflake glyph. Signals the plugin chain is
    // rendered to a freeze file and live processing is bypassed (CPU freed).
    if (track.isFrozen (te::Track::individualFreeze))
    {
        g.setColour (juce::Colour (0x3340a4d8));        // translucent icy blue
        g.fillRect (6, 0, getWidth() - 6, getHeight());
        g.setColour (juce::Colour (0xff9fd6f0));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x9d\x84"),   // snowflake U+2744
                    getWidth() - 18, 1, 16, 14, juce::Justification::centred, false);
    }

    // dB tick scale under the volume fader — the arrangement counterpart of
    // the mixer's scale column (user request 2026-06-13). Tick positions come
    // from the slider itself so they always align with the fader mapping.
    if (! dbScaleArea.isEmpty() && volumeSlider.isVisible()
        && dbScaleArea.getY() + 8 <= getHeight())
    {
        const float yTop = (float) dbScaleArea.getY();

        // Live peak meter bar behind the ticks — same data as the mixer strip.
        if (meterDb > -60.0f)
        {
            const float xMin = (float) volumeSlider.getX()
                               + volumeSlider.getPositionOfValue (-60.0);
            const float xLvl = (float) volumeSlider.getX()
                               + volumeSlider.getPositionOfValue (
                                     juce::jlimit (-60.0, 6.0, (double) meterDb));
            if (xLvl > xMin)
            {
                g.setColour (meterDb > 0.0f     ? juce::Colour (0xffd9544f)
                             : meterDb > -12.0f ? juce::Colour (0xffd9c34f)
                                                : juce::Colour (0xff58b85c));
                g.fillRect (xMin, yTop, xLvl - xMin, 3.0f);
            }
        }

        for (double v : { -60.0, -50.0, -40.0, -30.0, -20.0, -10.0, 0.0, 6.0 })
        {
            const bool major = (v == 0.0);
            const int  x     = volumeSlider.getX()
                               + (int) volumeSlider.getPositionOfValue (v);
            g.setColour (major ? juce::Colour (0xffaab1b9) : juce::Colour (0xff5a6068));
            g.drawVerticalLine (x, yTop, yTop + (major ? 4.0f : 3.0f));
        }

        g.setFont (juce::FontOptions (7.0f));
        g.setColour (juce::Colour (0xff8a9097));
        for (auto [v, txt] : { std::pair<double, const char*> { 0.0, "0" },
                               { -20.0, "20" }, { -40.0, "40" }, { -60.0, "60" } })
        {
            const int x = volumeSlider.getX()
                          + (int) volumeSlider.getPositionOfValue (v);
            g.drawText (txt, x - 9, dbScaleArea.getY() + 3, 18, 6,
                        juce::Justification::centred, false);
        }
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

void TrackHeader::applyScaleDrag (float x)
{
    const auto xMin = (float) volumeSlider.getX()
                      + volumeSlider.getPositionOfValue (volumeSlider.getMinimum());
    const auto xMax = (float) volumeSlider.getX()
                      + volumeSlider.getPositionOfValue (volumeSlider.getMaximum());
    if (xMax <= xMin)
        return;

    const double prop = juce::jlimit (0.0, 1.0, (double) (x - xMin) / (double) (xMax - xMin));
    // sendNotificationSync → onValueChange → MixerEngine::setVolume (undoable)
    volumeSlider.setValue (volumeSlider.proportionOfLengthToValue (prop),
                           juce::sendNotificationSync);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    // dB scale strip behaves like the mixer scale: click jumps the fader to
    // that value, dragging rides it (user feedback 2026-06-13).
    if (e.mods.isLeftButtonDown() && dbScaleArea.contains (e.getPosition()))
    {
        scaleDragActive = true;
        applyScaleDrag (e.position.x);
        return;
    }

    if (! e.mods.isRightButtonDown())
    {
        // Notify ArrangementView to update selection
        if (onSelectionRequest)
            onSelectionRequest (this, e.mods.isCommandDown());
    }

    if (e.mods.isRightButtonDown())
        showContextMenu();
}

void TrackHeader::mouseUp (const juce::MouseEvent&)
{
    scaleDragActive = false;
}

void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    // Riding the dB scale — follow the x position only (y may leave the strip)
    if (scaleDragActive)
    {
        applyScaleDrag (e.position.x);
        return;
    }

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

void TrackHeader::updateAccessibleNames()
{
    // Namen mit dem SPURNAMEN qualifizieren. Ein blosses "M" auf jeder Spur ist
    // fuer VoiceOver nutzlos ("Taste M" — welche?) und fuer eine Automatisierung
    // mehrdeutig: bei zwoelf Spuren gibt es zwoelf gleichnamige Elemente, und ein
    // Klick trifft irgendeines davon, ohne dass es auffaellt.
    // Der sichtbare Text bleibt "M"/"S"/"REC"/"I" — setTitle zeichnet nichts.
    // Der ZUSTAND gehoert mit in den Namen: „stumm" ist sonst nur eine Farbe —
    // fuer VoiceOver unhoerbar und fuer einen Testlauf unpruefbar. Genau daran
    // ist der Befund vom 2026-08-18 lange unentdeckt geblieben (Spurkopf zeigte
    // nach dem Laden nie den echten Mute-Zustand).
    const auto n = track.getName();
    const juce::String muteState = (bool) track.state.getProperty (te::IDs::mute) ? " (stumm)" : "";
    const juce::String soloState = (bool) track.state.getProperty (te::IDs::solo) ? " (solo)" : "";
    muteButton   .setTitle (juce::String::fromUTF8 ("Stumm ") + n + muteState);
    soloButton   .setTitle ("Solo " + n + soloState);
    armButton    .setTitle (juce::String::fromUTF8 ("Aufnahmebereit ") + n
                            + ((bool) track.state.getProperty ("armed", false) ? " an" : " aus"));
    monitorButton.setTitle (juce::String::fromUTF8 ("Eingang mithoeren ") + n);
    inputButton  .setTitle (juce::String::fromUTF8 ("Eingang waehlen ") + n);
    stereoButton .setTitle (juce::String::fromUTF8 ("Mono/Stereo ") + n);
    volumeSlider .setTitle (juce::String::fromUTF8 ("Lautstaerke ") + n);
    trackNameLabel.setTitle (juce::String::fromUTF8 ("Spurname ") + n);
    setTitle (juce::String::fromUTF8 ("Spur ") + n);
}

void TrackHeader::refresh()
{
    updateAccessibleNames();

    // This can run inside the ValueTree notification, before TE updates its
    // CachedValues. Read the committed properties so Undo displays the new state.
    muteButton.setToggleState ((bool) track.state.getProperty (te::IDs::mute), juce::dontSendNotification);
    soloButton.setToggleState ((bool) track.state.getProperty (te::IDs::solo), juce::dontSendNotification);

    armButton.setToggleState ((bool) track.state.getProperty ("armed", false), juce::dontSendNotification);

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

bool TrackHeader::isMidiTrack() const
{
    // Am TYP-MERKMAL, nicht mehr am Inhalt. Vorher galt eine Spur nur dann als
    // MIDI-Spur, wenn zufaellig ein MidiClip darauf lag — eine frisch angelegte
    // Instrumentenspur ohne Clip bot deshalb keinen MIDI-Eingang an, und der
    // Aufnahmeknopf verhielt sich wie bei Audio (User-Befund 2026-08-15).
    // Der Rueckfall auf den Inhalt steckt in isInstrumentTrack und haelt alte
    // Sessions am Leben.
    return aizana::isInstrumentTrack (track);
}

void TrackHeader::populateInputSelector()
{
    // Update tooltip to reflect the track type
    inputButton.setTooltip (isMidiTrack() ? juce::String::fromUTF8 ("MIDI-Eingang wählen")
                                          : juce::String::fromUTF8 ("Audioeingang wählen"));
    refreshInputButton();
}

void TrackHeader::showInputMenu()
{
    auto& dm = track.edit.engine.getDeviceManager();

    if (isMidiTrack())
    {
        // ---- MIDI track: show MidiInputDevices ----
        juce::PopupMenu menu;
        menu.addItem (1, "No Input", true, false);
        menu.addSeparator();

        int id = 2;
        for (auto& dev : dm.getMidiInDevices())
        {
            if (dev == nullptr) continue;
            bool isCurrent = false;
            for (auto* instance : track.edit.getAllInputDevices())
            {
                if (te::isOnTargetTrack (*instance, track, 0)
                    && &instance->getInputDevice() == dev.get())
                {
                    isCurrent = true;
                    break;
                }
            }
            menu.addItem (id++, dev->getName(), true, isCurrent);
        }

        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (&inputButton),
            [this] (int result)
            {
                if (result == 1) { assignMidiInput (nullptr); return; }
                if (result < 2)  return;

                auto& dm2 = track.edit.engine.getDeviceManager();
                int id2 = 2;
                for (auto& dev2 : dm2.getMidiInDevices())
                {
                    if (dev2 == nullptr) continue;
                    if (id2 == result) { assignMidiInput (dev2.get()); return; }
                    ++id2;
                }
                assignMidiInput (nullptr);
            });
        return;
    }

    // ---- Audio track: show Named Input Buses (Phase 2, Studio One model) ----
    // Tracks bind to named software buses, never to raw hardware. Each bus
    // resolves to a WaveInputDevice via its hardware channel; the assignment then
    // flows through the unchanged Phase 1 setTarget() path in assignInput().
    juce::ignoreUnused (dm);

    // Which WaveInputDevice currently targets this track? (drives the tick mark)
    te::WaveInputDevice* currentDev = nullptr;
    for (auto* instance : track.edit.getAllInputDevices())
    {
        if (te::isOnTargetTrack (*instance, track, 0))
        {
            currentDev = dynamic_cast<te::WaveInputDevice*> (&instance->getInputDevice());
            break;
        }
    }

    const auto buses = inputBusManager.getBuses();

    juce::PopupMenu menu;
    menu.addItem (1, "No Input", true, currentDev == nullptr);
    menu.addSeparator();

    // Bus item IDs are 2..(2+N-1); "Manage Buses…" uses a fixed high ID.
    static constexpr int kManageBusesId = 9000;
    for (int i = 0; i < buses.size(); ++i)
    {
        auto* dev = inputBusManager.resolveBusToDevice (buses[i]);
        const bool offline   = (dev == nullptr);
        const bool isCurrent = (dev != nullptr && dev == currentDev);
        const juce::String label = buses[i].name + (offline ? juce::String ("  (offline)")
                                                            : juce::String());
        // Offline = the bus's hardware channel is not present on the current
        // interface. Shown but disabled; its name stays defined in the session.
        menu.addItem (2 + i, label, ! offline, isCurrent);
    }

    menu.addSeparator();
    menu.addItem (kManageBusesId, juce::String::fromUTF8 ("Manage Buses\xe2\x80\xa6"));

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&inputButton),
        [this, buses] (int result)
        {
            if (result == 1)            { assignInput (nullptr); return; }
            if (result == kManageBusesId) { handleManageBuses();   return; }
            if (result < 2)             return;

            const int idx = result - 2;
            if (idx < 0 || idx >= buses.size())
                return;

            // Re-resolve at selection time — the device list may have changed since
            // the menu opened. Captured bus structs are stable; device pointers are not.
            if (auto* dev = inputBusManager.resolveBusToDevice (buses[idx]))
                assignInput (dev);
        });
}

void TrackHeader::assignMidiInput (te::MidiInputDevice* midiIn)
{
    auto& edit = track.edit;

    // Detach any existing assignment for this track
    for (auto* instance : edit.getAllInputDevices())
        if (te::isOnTargetTrack (*instance, track, 0))
            instance->setRecordingEnabled (track.itemID, false);

    if (midiIn == nullptr)
    {
        refreshInputButton();
        return;
    }

    for (auto* instance : edit.getAllInputDevices())
    {
        if (&instance->getInputDevice() == midiIn)
        {
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

    if ((bool) track.state.getProperty ("armed", false))
    {
        for (auto* instance : edit.getAllInputDevices())
            if (&instance->getInputDevice() == midiIn)
                instance->setRecordingEnabled (track.itemID, true);

        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<TrackHeader> (this), midiIn]()
        {
            if (safeThis == nullptr) return;
            if (! (bool) safeThis->track.state.getProperty ("armed", false)) return;
            for (auto* inst : safeThis->track.edit.getAllInputDevices())
                if (&inst->getInputDevice() == midiIn && te::isOnTargetTrack (*inst, safeThis->track, 0))
                    inst->setRecordingEnabled (safeThis->track.itemID, true);
        });
    }
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
            // Prefer the alias — applyToEngine() sets it to the Named Bus name, so the
            // header shows "Vocal Mic" rather than the raw device name ("Input 1+2").
            auto& dev = instance->getInputDevice();
            inputName = dev.getAlias().isNotEmpty() ? dev.getAlias() : dev.getName();
            break;
        }
    }

    // Truncate long device names to fit the narrow header
    if (inputName.length() > 18)
        inputName = inputName.substring (0, 17) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));

    inputButton.setButtonText (inputName);

    // Den ZUGEWIESENEN Eingang im Namen mitfuehren. Der Knopf zeigte ihn nur als
    // Beschriftung — damit war von aussen nicht pruefbar, OB eine Zuweisung
    // ueberhaupt zustande kam. Genau daran haette man den Fehler "ich hoere mein
    // Spiel, aber es wird nicht aufgenommen" sofort gesehen.
    inputButton.setTitle (juce::String::fromUTF8 ("Eingang waehlen ") + track.getName()
                          + ": " + inputName);

    inputButton.setTooltip (isMidiTrack() ? juce::String::fromUTF8 ("MIDI-Eingang wählen")
                                          : juce::String::fromUTF8 ("Audioeingang wählen"));

    // Keep the Mono/Stereo button in sync with whatever input is now assigned.
    refreshStereoButton();
}

void TrackHeader::handleManageBuses()
{
    // Opens the Named Bus manager (create / rename / map hardware channel / delete).
    AudioIOSetupDialog::show (track.edit.engine, inputBusManager);
}

//==============================================================================

void TrackHeader::refreshStereoButton()
{
    if (isMidiTrack())
        return;

    const bool stereo = (bool) track.state.getProperty (kTrackStereoProp, false);
    stereoButton.setButtonText (stereo ? "St" : "M");
    stereoButton.setToggleState (stereo, juce::dontSendNotification);
}

void TrackHeader::toggleTrackStereo()
{
    // Clicking selects the track (Studio One behaviour) so S/M/R shortcuts target it.
    if (onSelectionRequest) onSelectionRequest (this, false);

    // Pure track channel-format flip — stored on the track state, undoable. It does
    // NOT reconfigure the hardware input (no setDeviceInChannelStereo), so it never
    // consumes a neighbouring channel or takes another track's input offline. Real
    // 2-channel stereo INPUT is a deliberate Bus-manager setting, decoupled from this.
    const bool newStereo = ! (bool) track.state.getProperty (kTrackStereoProp, false);

    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction (newStereo ? "Set Track Stereo" : "Set Track Mono");
    track.state.setProperty (kTrackStereoProp, newStereo, &um);

    // Functional effect (DAW-standard pan law): a MONO track uses a centre-compensated
    // pan law so a panned mono source keeps perceived loudness across the stereo field;
    // a STEREO track uses linear balance (pan just trims one side, no centre boost).
    // Applied to the track's VolumeAndPanPlugin in the same undo transaction.
    if (auto* vp = track.getVolumePlugin())
        vp->panLaw.setValue ((int) (newStereo ? te::PanLawLinear : te::PanLaw3dBCenter), &um);

    refreshStereoButton();
}

//==============================================================================

void TrackHeader::showContextMenu()
{
    juce::PopupMenu instrumentMenu;
    instrumentMenu.addItem (30, "Drums (Drum Machine)");
    instrumentMenu.addItem (31, "Synth (Poly)");

    juce::PopupMenu builtInMenu;
    builtInMenu.addItem (10, "EQ (4-Band Parametric)");
    builtInMenu.addItem (11, "Compressor");
    builtInMenu.addItem (12, "Reverb");
    builtInMenu.addItem (13, "Limiter");
    builtInMenu.addItem (14, "Delay");
    builtInMenu.addItem (15, "Gate");
    builtInMenu.addItem (16, "Saturator");
    builtInMenu.addItem (17, "De-Esser");

    // Track colour submenu (closes GAP-27): palette swatches + custom picker + reset.
    juce::PopupMenu colourMenu;
    populateColourMenu (colourMenu);

    juce::PopupMenu menu;
    menu.addSubMenu ("Instrument", instrumentMenu);
    menu.addSubMenu ("Built-in FX", builtInMenu);
    menu.addSeparator();
    menu.addItem (1, "Add Plugin...");
    menu.addSeparator();
    menu.addItem (2, "Create Bus from Track");
    menu.addItem (6, "Create FX Channel");
    menu.addItem (3, "Set Sidechain Source...");
    menu.addSeparator();

    // Freeze (Bounce-in-Place): renders the track's plugin chain to a file and
    // bypasses live processing to free CPU. TE refuses freeze on bus-routed tracks,
    // so offer it only when the track is freezable (or already frozen, to unfreeze).
    const bool isFrozen = track.isFrozen (te::Track::individualFreeze);
    menu.addItem (7,
                  isFrozen ? juce::String::fromUTF8 ("Auftauen (Unfreeze)")
                           : juce::String::fromUTF8 ("Spur einfrieren (Freeze)"),
                  isFrozen || aizana::canFreezeIndividually (track),
                  isFrozen);
    menu.addSeparator();

    // T1.3 Edit-Gruppen: Split/Delete wirken auf alle Spuren derselben Gruppe
    // (zeitgleiche Clips — der Multimic-Drum-Workflow).
    {
        juce::PopupMenu groupMenu;
        const int currentGroup = ClipEditor::getEditGroup (track);
        groupMenu.addItem (200, "Keine", true, currentGroup == 0);
        for (int g = 1; g <= ClipEditor::kMaxEditGroups; ++g)
            groupMenu.addItem (200 + g, "Gruppe " + juce::String (g), true, currentGroup == g);
        menu.addSubMenu ("Edit-Gruppe", groupMenu);
    }

    menu.addSeparator();
    menu.addSubMenu ("Track-Farbe", colourMenu);
    menu.addItem (4, "Rename Track");
    menu.addItem (5, "Delete Track");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result)
        {
            if (result >= 100 && result < 100 + AizanaTrackColours::paletteSize())
            {
                handleSetTrackColour (AizanaTrackColours::palette (result - 100));
                return;
            }

            if (result >= 200 && result <= 200 + ClipEditor::kMaxEditGroups)
            {
                ClipEditor::setEditGroup (track, result - 200);
                repaint();   // Badge aktualisieren
                return;
            }

            switch (result)
            {
                case 1:  handleAddPlugin();                             break;
                case 2:  handleCreateBus();                            break;
                case 6:  handleCreateFXChannel();                      break;
                case 3:  handleSetSidechainSource();                   break;
                case 7:  handleToggleFreeze();                         break;
                case 4:  handleRenameTrack();                          break;
                case 5:  handleDeleteTrack();                          break;
                case 10: handleInsertBuiltInFX ("Aizana EQ");         break;
                case 11: handleInsertBuiltInFX ("Aizana Compressor"); break;
                case 12: handleInsertBuiltInFX ("Aizana Reverb");     break;
                case 13: handleInsertBuiltInFX ("Aizana Limiter");    break;
                case 14: handleInsertBuiltInFX ("Aizana Delay");      break;
                case 15: handleInsertBuiltInFX ("Aizana Gate");       break;
                case 16: handleInsertBuiltInFX ("Aizana Saturator");  break;
                case 17: handleInsertBuiltInFX ("Aizana De-Esser");   break;
                case 30: handleInsertBuiltInFX ("Aizana Drums");      break;
                case 31: handleInsertBuiltInFX ("Aizana Synth");      break;
                case 98: handleCustomColourPicker();                   break;
                case 99: handleSetTrackColour (juce::Colour());        break;  // reset → auto palette
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

void TrackHeader::handleToggleFreeze()
{
    const bool nowFrozen = track.isFrozen (te::Track::individualFreeze);

    // Guard: freezing a track with no clips renders nothing useful and only
    // exercises a degenerate offline-render path — skip it with a hint.
    if (! nowFrozen && track.getClips().isEmpty())
    {
        track.edit.engine.getUIBehaviour().showWarningMessage (
            juce::String::fromUTF8 ("Nichts zum Einfrieren – diese Spur hat keinen Clip."));
        return;
    }

    // Flipping frozenIndividually triggers AudioTrack::freezeTrack() synchronously,
    // which renders the track via the Renderer. AizanaUIBehaviour now PUMPS the
    // message loop during that render, so the render may itself dispatch events
    // (incl. a rebuildTrackRows that deletes THIS header). Therefore:
    //   - defer onto a clean tick (out of the popup-menu teardown), and
    //   - drive the mutation through the AudioTrack pointer (which outlives this
    //     component), only touching `this` again behind a SafePointer guard.
    // Freeze is intentionally NOT undoable (TE design: null-UM CachedValue, the
    // freeze file is a regenerable cache — like Logic / Studio One).
    auto* trackPtr = &track;
    juce::Component::SafePointer<TrackHeader> safe (this);
    juce::MessageManager::callAsync ([safe, trackPtr, nowFrozen]
    {
        if (trackPtr == nullptr) return;
        trackPtr->setFrozen (! nowFrozen, te::Track::individualFreeze);
        if (safe != nullptr)
            safe->repaint();
    });
}

void TrackHeader::populateColourMenu (juce::PopupMenu& menu)
{
    // IDs: 100..100+paletteSize() = swatches; 98 = custom picker; 99 = reset to auto.
    for (int i = 0; i < AizanaTrackColours::paletteSize(); ++i)
        menu.addColouredItem (100 + i, AizanaTrackColours::paletteName (i),
                              AizanaTrackColours::palette (i).brighter (0.3f));
    menu.addSeparator();
    menu.addItem (98, juce::String::fromUTF8 ("Custom\xe2\x80\xa6"));
    menu.addItem (99, "Standard (Auto)");
}

void TrackHeader::showTrackColourMenu()
{
    // Standalone colour menu shown when the left colour bar is clicked (Studio One style).
    juce::PopupMenu menu;
    populateColourMenu (menu);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&colourBar),
        [this] (int result)
        {
            if (result >= 100 && result < 100 + AizanaTrackColours::paletteSize())
                handleSetTrackColour (AizanaTrackColours::palette (result - 100));
            else if (result == 98) handleCustomColourPicker();
            else if (result == 99) handleSetTrackColour (juce::Colour());
        });
}

void TrackHeader::handleSetTrackColour (juce::Colour newColour)
{
    // setColour() writes the colour CachedValue, which is bound to the edit's
    // UndoManager — begin a transaction so it is a discrete undo step. An empty
    // colour resets to the near-black default → the shared resolver shows the
    // auto palette colour again.
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction ("Track Colour");
    track.setColour (newColour);

    repaint();
    if (onColourChanged)
        onColourChanged();   // repaints the row wash + clips so all three agree
}

void TrackHeader::handleCustomColourPicker()
{
    // A ColourSelector hosted in a CallOutBox; applies live so the row updates
    // as the user drags. All changes coalesce into one undo transaction.
    struct ColourCallout : public juce::Component,
                           private juce::ChangeListener
    {
        juce::ColourSelector selector { juce::ColourSelector::showColourAtTop
                                      | juce::ColourSelector::showColourspace
                                      | juce::ColourSelector::showSliders };
        std::function<void (juce::Colour)> onColour;

        explicit ColourCallout (juce::Colour initial)
        {
            selector.setCurrentColour (initial, juce::dontSendNotification);
            selector.addChangeListener (this);
            addAndMakeVisible (selector);
            setSize (280, 300);
        }

        void resized() override { selector.setBounds (getLocalBounds()); }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (onColour)
                onColour (selector.getCurrentColour());
        }
    };

    track.edit.getUndoManager().beginNewTransaction ("Track Colour");

    auto content = std::make_unique<ColourCallout> (AizanaTrackColours::displayColour (track));
    juce::Component::SafePointer<TrackHeader> safe (this);
    content->onColour = [safe] (juce::Colour c)
    {
        if (safe == nullptr)
            return;
        safe->track.setColour (c);
        safe->repaint();
        if (safe->onColourChanged)
            safe->onColourChanged();
    };

    juce::CallOutBox::launchAsynchronously (std::move (content), getScreenBounds(), nullptr);
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
            if (result <= 0)  // dismissed
                return;

            // Find the first sidechain-capable plugin in this track's chain.
            te::Plugin* target = nullptr;
            for (auto* plugin : pluginManager.getPluginChain (track))
            {
                if (plugin->canSidechain())
                {
                    target = plugin;
                    break;
                }
            }

            if (target == nullptr)
                return;

            if (result == 1)  // (None) → actually clear (was a no-op before)
            {
                mixerEngine.clearSidechainSource (*target);
                return;
            }

            // result == 2 → otherTracks[0], result == 3 → otherTracks[1], etc.
            const int trackIndex = result - 2;
            if (trackIndex < 0 || trackIndex >= otherTracks.size())
                return;

            mixerEngine.setSidechainSource (*target, *otherTracks[trackIndex]);
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
    if (onMixToggleRequest) { onMixToggleRequest (this, false); return; }
    // Clicking any button implicitly selects the track (Studio One behaviour),
    // so subsequent S/M keyboard shortcuts know which track to target.
    if (onSelectionRequest) onSelectionRequest (this, false);
    mixerEngine.setMute (track, muteButton.getToggleState());
    updateAccessibleNames();          // der Zustand steht mit im Namen
}

void TrackHeader::soloClicked()
{
    if (onMixToggleRequest) { onMixToggleRequest (this, true); return; }
    if (onSelectionRequest) onSelectionRequest (this, false);
    mixerEngine.setSolo (track, soloButton.getToggleState());
    updateAccessibleNames();
}

void TrackHeader::armClicked()
{
    const bool armed = armButton.getToggleState();
    track.state.setProperty ("armed", armed, nullptr);

    auto& edit = track.edit;

    if (armed)
    {
        // ---- Instrumentenspur: das virtuelle Keyboard scharf schalten -------
        //
        // Vorher lief hier NUR der Audio-Zweig: er suchte die erste beliebige
        // Instanz auf der Spur bzw. griff sonst zum ersten Wave-Eingang. Fuer
        // eine Instrumentenspur bedeutete das im schlechtesten Fall, dass ein
        // AUDIO-Eingang scharfgeschaltet wurde und das gespielte MIDI nirgends
        // landete — genau der Zustand "ich hoere mein Spiel, aber es wird nicht
        // aufgenommen" (User-Befund 2026-08-16).
        if (aizana::isInstrumentTrack (track))
        {
            if (aizana::armKeyboardForTrack (track))
            {
                // setTarget feuert einen AsyncUpdate, der den Aufnahme-Schalter
                // wieder abraeumen kann — deshalb NOCH EINMAL hinterher (I-08),
                // mit nullptr-Guard als Erstes im Lambda.
                juce::MessageManager::callAsync (
                    [safe = juce::Component::SafePointer<TrackHeader> (this)]
                    {
                        if (safe == nullptr) return;
                        if (! (bool) safe->track.state.getProperty ("armed", false)) return;
                        aizana::armKeyboardForTrack (safe->track);
                    });

                refreshInputButton();
                return;
            }
        }

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

        // ARM does NOT auto-enable monitoring (Studio One 7 / Logic Pro X standard).
        // Monitoring is controlled exclusively by the I button — decoupled from REC.
        // This prevents the feedback loop when built-in mic + speakers are the same device.
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
