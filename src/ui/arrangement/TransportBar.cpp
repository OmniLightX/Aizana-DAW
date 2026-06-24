#include "TransportBar.h"
#include "MetronomeSettingsPanel.h"

//==============================================================================

TransportBar::TransportBar (ApplicationState& appState_)
    : appState (appState_)
{
    // Style position label
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setText ("0:00:00.000", juce::dontSendNotification);
    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    positionLabel.setColour (juce::Label::textColourId, juce::Colours::white);

    // Style BPM label
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::FontOptions (14.0f));
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    // Make BPM label editable: double-click enters edit mode
    bpmLabel.setEditable (false, true, false);  // (singleClick, doubleClick, lossOfFocusDiscards)
    bpmLabel.onTextChange = [this]
    {
        auto newBpm = bpmLabel.getText().getDoubleValue();
        if (newBpm >= 20.0 && newBpm <= 300.0)
        {
            if (auto* edit = appState.editManager.getEdit())
            {
                auto& undoManager = appState.editManager.getUndoManager();
                undoManager.beginNewTransaction ("Set BPM");
                auto& seq = edit->tempoSequence;
                if (seq.getNumTempos() > 0)
                    seq.getTempo (0)->setBpm (newBpm);
            }
        }
        // Refresh label with the actually-applied value
        updateBpmLabel();
    };

    // Update BPM display from edit tempo
    updateBpmLabel();

    // ---- Time signature — single clean "4/4" button, click → common signatures ----
    timeSigButton.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    timeSigButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    timeSigButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffcccccc));
    timeSigButton.setColour (juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    timeSigButton.setTooltip (juce::String::fromUTF8 ("Taktart \xe2\x80\x94 klicken zum \xc3\x84ndern"));
    timeSigButton.onClick = [this] { showTimeSigMenu(); };

    updateTimeSigDisplay();

    // Style sample rate label — brighter text + pointer cursor signals interactivity
    sampleRateLabel.setJustificationType (juce::Justification::centred);
    sampleRateLabel.setFont (juce::FontOptions (11.0f));
    sampleRateLabel.setColour (juce::Label::textColourId, juce::Colour (0xffaaaaaa));
    sampleRateLabel.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    sampleRateLabel.setText ("-- kHz", juce::dontSendNotification);

    // ---- Transport icon buttons (Sketch 005) — colours per CloudesIcon semantics ----
    auto neutral = [] (IconButton& b)
    {
        b.bgColour     = juce::Colour (0xff3a3a3a);
        b.iconColour   = juce::Colour (0xff9aa0a6);
        b.iconColourOn = juce::Colours::white;
    };

    neutral (rewindButton);  rewindButton.setTooltip ("Zum Start");
    neutral (stopButton);    stopButton.setTooltip ("Stopp");

    // Play — green
    playButton.bgColour     = juce::Colour (0xff2d6a2d);
    playButton.bgColourOn   = juce::Colour (0xff3a8f3a);   // brighter while playing
    playButton.iconColour   = juce::Colour (0xffe6ffe6);
    playButton.iconColourOn = juce::Colours::white;
    playButton.setTooltip ("Wiedergabe");

    // Record — red icon; fills red while recording
    recordButton.bgColour     = juce::Colour (0xff3a3a3a);
    recordButton.bgColourOn   = juce::Colour (0xffcc3333);
    recordButton.iconColour   = juce::Colour (0xffe0584d);
    recordButton.iconColourOn = juce::Colours::white;
    recordButton.setTooltip ("Aufnahme");

    // Wire transport
    rewindButton.onClick = [this] { rewindClicked(); };
    playButton.onClick   = [this] { playClicked(); };
    stopButton.onClick   = [this] { stopClicked(); };
    recordButton.onClick = [this] { recordClicked(); };

    // Metronome button — toggle, wired to MetronomeGenerator (blue when on)
    neutral (metronomeButton);
    metronomeButton.bgColourOn = juce::Colour (0xff2d4a6a);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setTooltip ("Metronom");
    metronomeButton.onClick = [this]
    {
        const bool enabled = metronomeButton.getToggleState();
        appState.audioEngine.getMetronome().setEnabled (enabled);
    };

    // Loop button — toggles transport looping (primary-blue when on); synced by timerCallback
    neutral (loopButton);
    loopButton.bgColourOn = juce::Colour (0xff4488ff);
    loopButton.setClickingTogglesState (false);   // manual toggle via onClick
    loopButton.setTooltip ("Loop");
    loopButton.onClick = [this]
    {
        auto* edit = appState.editManager.getEdit();
        if (edit == nullptr) return;
        auto& transport = edit->getTransport();
        bool newLooping = ! transport.looping;
        transport.looping = newLooping;
        loopButton.setToggleState (newLooping, juce::dontSendNotification);
    };

    // Tap Tempo button
    neutral (tapTempoButton);
    tapTempoButton.setTooltip ("Tap Tempo — im Takt klicken (Reset nach 2 s)");
    tapTempoButton.onClick = [this] { tapTempoClicked(); };

    // Auto-Punch — icon toggle; record only within the Loop In/Out markers (red when armed).
    neutral (autoPunchButton);
    autoPunchButton.bgColourOn   = juce::Colour (0xffcc3333);
    autoPunchButton.iconColour   = juce::Colour (0xffe0584d);
    autoPunchButton.iconColourOn = juce::Colours::white;
    autoPunchButton.setClickingTogglesState (false);   // invariant I-04: drive state ourselves
    autoPunchButton.setTooltip ("Auto-Punch: Aufnahme nur zwischen den Loop In/Out-Markern");
    autoPunchButton.onClick = [this] { toggleAutoPunch(); };
    updateAutoPunchButton();

    // Metronome settings ▾ — opens the Vorzähler / Pre-Roll / Accent / Level popover (Sketch 006 A).
    neutral (metroMenuButton);
    metroMenuButton.iconScale = 0.40f;
    metroMenuButton.setTooltip (juce::String::fromUTF8 ("Metronom & Aufnahme: Vorz\xc3\xa4hler, Pre-Roll, Downbeat-Accent, Klick-Lautst\xc3\xa4rke"));
    metroMenuButton.onClick = [this] { showMetronomeMenu(); };

    // ---- The Dial (Phase 2, Block A): switch to Beginner + assist toggle ----
    modeButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff3a3a3a));
    modeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcccccc));
    modeButton.setTooltip ("Zur Einsteiger-Welt wechseln (gleiche Session)");
    modeButton.onClick = [this] { appState.uiState.setMode (UIStateManager::UIMode::Beginner); };

    assistButton.setClickingTogglesState (false);   // invariant I-04: drive state ourselves
    assistButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    assistButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1fa882));  // teal when on
    assistButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff888888));
    assistButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    assistButton.setTooltip ("KI-Assist ein-/ausblenden (auch im Pro-Modus)");
    assistButton.onClick = [this] { appState.uiState.toggleAssist(); };

    // Edit/Mix view switcher (Studio One pattern, adapted): segmented pair,
    // active segment highlighted; actual layout change is owned by MainComponent.
    for (auto* b : { &editViewButton, &mixViewButton })
    {
        b->setClickingTogglesState (false);   // invariant I-04
        b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a6a8a));
        b->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff999999));
        b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    }
    editViewButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    mixViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    editViewButton.setTooltip (juce::String::fromUTF8 ("Edit-Ansicht — Arrangement in voller Höhe"));
    mixViewButton.setTooltip (juce::String::fromUTF8 ("Mix-Ansicht — Mixer groß einblenden"));
    editViewButton.onClick = [this] { if (onSelectViewMode) onSelectViewMode (false); };
    mixViewButton.onClick  = [this] { if (onSelectViewMode) onSelectViewMode (true); };

    // KI-Mix co-pilot — accent button (teal = "AI acts"); distinct from the Mix view toggle.
    kiMixButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff1fa882));
    kiMixButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    kiMixButton.setTooltip (juce::String::fromUTF8 (
        "KI-Mix — automatischer Mix-Vorschlag (Einsteiger: sofort · Pro: Vorschau)"));
    kiMixButton.onClick = [this] { if (onMixRequested) onMixRequested(); };

    // KI-Studio — purple accent (the in-app AI assistant: chat + generate plugins).
    kiStudioButton.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xff7c5cff));
    kiStudioButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    kiStudioButton.setTooltip (juce::String::fromUTF8 (
        "KI-Studio — Chat mit Claude/Codex: Plugins bauen lassen (mixen folgt)"));
    kiStudioButton.onClick = [this] { if (onAiStudioRequested) onAiStudioRequested(); };

    addAndMakeVisible (rewindButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (metronomeButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (tapTempoButton);
    addAndMakeVisible (metroMenuButton);
    addAndMakeVisible (autoPunchButton);
    addAndMakeVisible (modeButton);
    addAndMakeVisible (assistButton);
    addAndMakeVisible (kiMixButton);
    addAndMakeVisible (kiStudioButton);
    addAndMakeVisible (editViewButton);
    addAndMakeVisible (mixViewButton);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (timeSigButton);
    addAndMakeVisible (sampleRateLabel);

    // Start 30ms timer to update position display during playback
    startTimer (30);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced (4);

    // Left side: transport icon buttons — square, grouped (Sketch 005).
    const int bw      = 34;
    const int buttonH = area.getHeight();
    auto place = [&] (juce::Component& b, int gap)
    {
        b.setBounds (area.removeFromLeft (bw).withHeight (buttonH));
        area.removeFromLeft (gap);
    };

    place (rewindButton, 3);
    place (playButton,   3);
    place (stopButton,   3);
    place (recordButton, 10);   // gap before the secondary cluster

    // Metronome + its settings ▾ (split button, hugged together), then Auto-Punch, Loop, Tap.
    place (metronomeButton, 0);
    metroMenuButton.setBounds (area.removeFromLeft (16).withHeight (buttonH));
    area.removeFromLeft (8);
    place (autoPunchButton, 8);
    place (loopButton,   3);
    place (tapTempoButton, 3);

    // Right side: Dial (mode + assist) → Edit/Mix switcher → sample rate → BPM
    // → time sig (rightmost to leftmost)
    modeButton.setBounds   (area.removeFromRight (104).withHeight (buttonH));
    area.removeFromRight (4);
    assistButton.setBounds (area.removeFromRight (62).withHeight (buttonH));
    area.removeFromRight (10);
    kiStudioButton.setBounds (area.removeFromRight (78).withHeight (buttonH));
    area.removeFromRight (6);
    kiMixButton.setBounds  (area.removeFromRight (66).withHeight (buttonH));
    area.removeFromRight (10);
    mixViewButton.setBounds  (area.removeFromRight (46).withHeight (buttonH));
    editViewButton.setBounds (area.removeFromRight (46).withHeight (buttonH));
    area.removeFromRight (10);
    sampleRateLabel.setBounds    (area.removeFromRight (70));
    area.removeFromRight (4);
    bpmLabel.setBounds           (area.removeFromRight (100));
    area.removeFromRight (4);
    // Time signature: a single clean "4/4" button left of BPM.
    timeSigButton.setBounds (area.removeFromRight (52).withHeight (buttonH));
    area.removeFromRight (4);

    // Centre: position display
    positionLabel.setBounds (area);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // Bottom border to visually separate from arrangement
    g.setColour (juce::Colour (0xff000000));
    g.drawLine (0.0f, (float)getHeight() - 1.0f, (float)getWidth(), (float)getHeight() - 1.0f, 1.0f);
}

void TransportBar::timerCallback()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    auto pos = transport.getPosition();

    // Format as M:SS:BB.mmm (bars not available without tempo analysis; use H:MM:SS.mmm)
    const double totalSeconds = pos.inSeconds();
    const int hours   = (int)(totalSeconds / 3600.0);
    const int minutes = (int)(std::fmod (totalSeconds, 3600.0) / 60.0);
    const int seconds = (int)std::fmod (totalSeconds, 60.0);
    const int millis  = (int)(std::fmod (totalSeconds, 1.0) * 1000.0);

    auto text = juce::String::formatted ("%d:%02d:%02d.%03d", hours, minutes, seconds, millis);
    positionLabel.setText (text, juce::dontSendNotification);

    // Toggle button states based on playback
    playButton.setToggleState (transport.isPlaying(), juce::dontSendNotification);
    recordButton.setToggleState (transport.isRecording(), juce::dontSendNotification);
    loopButton.setToggleState (transport.looping.get(), juce::dontSendNotification);

    // Keep the Dial's assist toggle in sync (assist can change from the Beginner view too)
    assistButton.setToggleState (appState.uiState.isAssistEnabled(), juce::dontSendNotification);

    // Edit/Mix segment highlight follows the actual mixer state (Cmd+M, divider)
    if (isMixerShown)
    {
        const bool mix = isMixerShown();
        editViewButton.setToggleState (! mix, juce::dontSendNotification);
        mixViewButton.setToggleState (mix, juce::dontSendNotification);
    }

    // Keep BPM label and time signature combos in sync (handles undo/redo changes)
    if (! bpmLabel.isBeingEdited())
    {
        updateBpmLabel();
        updateTimeSigDisplay();
        updateAutoPunchButton();

        // Keep metronome BPM in sync with the edit's current tempo
        if (auto* editForBpm = appState.editManager.getEdit())
        {
            auto& seq = editForBpm->tempoSequence;
            if (seq.getNumTempos() > 0)
            {
                const double bpm = seq.getTempo (0)->getBpm();
                appState.audioEngine.getMetronome().setBpm (bpm);

                // Sync beats per bar from time signature
                if (seq.getNumTimeSigs() > 0)
                    appState.audioEngine.getMetronome().setBeatsPerBar (
                        seq.getTimeSig (0)->numerator);
            }
        }
    }

    // Sample rate update — every ~2 seconds (60 ticks at 30ms = 1.8s)

    if (++sampleRateUpdateCounter >= 60)
    {
        sampleRateUpdateCounter = 0;
        auto* device = appState.audioEngine.getDeviceManager().getCurrentAudioDevice();
        if (device != nullptr)
        {
            const double sr = device->getCurrentSampleRate();
            const juce::String srText = (sr >= 1000.0)
                ? juce::String (sr / 1000.0, 1) + " kHz"
                : juce::String (sr, 0) + " Hz";
            sampleRateLabel.setText (srText, juce::dontSendNotification);
        }
    }
}

void TransportBar::tapTempoClicked()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // Reset sequence if >2000ms since last tap
    if (! tapTimes.isEmpty() && (now - tapTimes.getLast()) > 2000.0)
        tapTimes.clear();

    tapTimes.add (now);

    // Keep only the last 4 taps
    while (tapTimes.size() > 4)
        tapTimes.remove (0);

    // Need at least 2 taps to compute a tempo
    if (tapTimes.size() < 2)
        return;

    // Average interval between consecutive taps (ms)
    double totalMs = 0.0;
    for (int i = 1; i < tapTimes.size(); ++i)
        totalMs += tapTimes[i] - tapTimes[i - 1];
    const double avgIntervalMs = totalMs / (tapTimes.size() - 1);

    if (avgIntervalMs <= 0.0)
        return;

    const double newBpm = juce::jlimit (30.0, 250.0, 60000.0 / avgIntervalMs);

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    auto& undoManager = appState.editManager.getUndoManager();
    undoManager.beginNewTransaction ("Tap Tempo");
    auto& seq = edit->tempoSequence;
    if (seq.getNumTempos() > 0)
        seq.getTempo (0)->setBpm (newBpm);
}

void TransportBar::updateBpmLabel()
{
    if (auto* edit = appState.editManager.getEdit())
    {
        auto& seq = edit->tempoSequence;
        if (seq.getNumTempos() > 0)
        {
            const double bpm = seq.getTempo (0)->getBpm();
            bpmLabel.setText (juce::String (bpm, 1) + " BPM", juce::dontSendNotification);
        }
    }
}

void TransportBar::updateTimeSigDisplay()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    auto& seq = edit->tempoSequence;
    if (seq.getNumTimeSigs() == 0) return;

    const int num = seq.getTimeSig (0)->numerator;
    const int den = seq.getTimeSig (0)->denominator;
    timeSigButton.setButtonText (juce::String (num) + "/" + juce::String (den));
}

void TransportBar::applyTimeSig (int numerator, int denominator)
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr) return;

    auto& seq = edit->tempoSequence;
    if (seq.getNumTimeSigs() > 0)
    {
        appState.editManager.getUndoManager().beginNewTransaction ("Set time signature");
        seq.getTimeSig (0)->setStringTimeSig (juce::String (numerator) + "/" + juce::String (denominator));
    }

    appState.audioEngine.getMetronome().setBeatsPerBar (numerator);  // keep metronome in sync
    updateTimeSigDisplay();
}

void TransportBar::showTimeSigMenu()
{
    auto* edit = appState.editManager.getEdit();
    const int curNum = (edit != nullptr && edit->tempoSequence.getNumTimeSigs() > 0)
                           ? edit->tempoSequence.getTimeSig (0)->numerator.get()   : 4;
    const int curDen = (edit != nullptr && edit->tempoSequence.getNumTimeSigs() > 0)
                           ? edit->tempoSequence.getTimeSig (0)->denominator.get() : 4;

    // Common signatures (Studio One-style quick list). id encodes num*100+den.
    static const std::pair<int,int> kSigs[] = {
        {4,4}, {3,4}, {2,4}, {6,8}, {9,8}, {12,8}, {5,4}, {7,8}, {6,4}, {2,2}
    };

    juce::PopupMenu menu;
    for (auto [n, d] : kSigs)
    {
        const bool ticked = (n == curNum && d == curDen);
        menu.addItem (n * 100 + d, juce::String (n) + "/" + juce::String (d), true, ticked);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSigButton),
        [this] (int result)
        {
            if (result <= 0) return;
            applyTimeSig (result / 100, result % 100);
        });
}

void TransportBar::showMetronomeMenu()
{
    // Sketch 006 A: one popover bundles Vorzähler + Pre-Roll + Downbeat accent +
    // click level, opened from the metronome ▾. All state via SafePointer-guarded
    // callbacks so the panel (owned by the CallOutBox) is lifetime-safe.
    using CI = tracktion::engine::Edit::CountIn;

    auto panel = std::make_unique<MetronomeSettingsPanel>();
    juce::Component::SafePointer<TransportBar> safe (this);

    panel->getCountIn = [safe]() -> int
    {
        if (safe == nullptr) return 0;
        if (auto* e = safe->appState.editManager.getEdit())
            switch (e->getCountInMode()) { case CI::oneBar: return 1; case CI::twoBar: return 2; default: break; }
        return 0;
    };
    panel->setCountIn = [safe] (int n)
    {
        if (safe == nullptr) return;
        if (auto* e = safe->appState.editManager.getEdit())
            e->setCountInMode (n == 1 ? CI::oneBar : n == 2 ? CI::twoBar : CI::none);
    };

    panel->getPreRoll = [safe]() -> int { return safe != nullptr ? safe->preRollBars : 0; };
    panel->setPreRoll = [safe] (int n) { if (safe != nullptr) safe->preRollBars = juce::jlimit (0, 2, n); };

    panel->getAccent = [safe]() -> bool { return safe != nullptr && safe->appState.audioEngine.getMetronome().getAccentEnabled(); };
    panel->setAccent = [safe] (bool b) { if (safe != nullptr) safe->appState.audioEngine.getMetronome().setAccentEnabled (b); };
    panel->getLevel  = [safe]() -> float { return safe != nullptr ? safe->appState.audioEngine.getMetronome().getLevel() : 1.0f; };
    panel->setLevel  = [safe] (float l) { if (safe != nullptr) safe->appState.audioEngine.getMetronome().setLevel (l); };

    panel->refresh();
    juce::CallOutBox::launchAsynchronously (std::move (panel),
                                            metroMenuButton.getScreenBounds(), nullptr);
}

void TransportBar::toggleAutoPunch()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    edit->recordingPunchInOut.setValue (! edit->recordingPunchInOut.get(), nullptr);
    updateAutoPunchButton();
}

void TransportBar::updateAutoPunchButton()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    autoPunchButton.setToggleState (edit->recordingPunchInOut.get(), juce::dontSendNotification);
}

void TransportBar::rewindClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    transport.stop (false, false);
    transport.setPosition (tracktion::core::TimePosition::fromSeconds (0.0));
}

void TransportBar::playClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    if (transport.isPlaying())
        transport.stop (false, false);
    else
        transport.play (false);
}

void TransportBar::stopClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    // Stop at current position. Use the dedicated Rewind button (|<) to return to start.
    edit->getTransport().stop (false, false);
}

void TransportBar::recordClicked()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    if (transport.isRecording())
    {
        transport.stop (false, false);
        return;
    }

    // Pre-Roll: roll the transport back N bars before the record start so the
    // performer hears a run-up. With Auto-Punch the recorded region stays the
    // loop range (TE punches in/out), so the run-up itself is not captured.
    if (preRollBars > 0)
    {
        auto& ts = edit->tempoSequence;
        const auto recStart = edit->recordingPunchInOut.get()
                                  ? transport.getLoopRange().getStart()
                                  : transport.getPosition();
        const auto bb        = ts.toBarsAndBeats (recStart);
        const int  targetBar = juce::jmax (0, bb.bars - preRollBars);
        transport.setPosition (ts.toTime (tracktion::tempo::BarsAndBeats { targetBar }));
    }

    transport.record (false);
}

void TransportBar::mouseDown (const juce::MouseEvent& e)
{
    // Only respond to clicks on the sample rate label
    if (sampleRateLabel.getBounds().contains (e.getPosition()))
        showSampleRateMenu();
}

void TransportBar::showSampleRateMenu()
{
    auto& deviceManager = appState.audioEngine.getDeviceManager();
    auto* device = deviceManager.getCurrentAudioDevice();
    const double currentRate = (device != nullptr) ? device->getCurrentSampleRate() : 0.0;

    // Standard professional rates only — clean popup like iOS Settings
    // Apple approach: present only the meaningful choices, no technical noise
    struct RateOption { double rate; const char* label; };
    const RateOption options[] = {
        { 44100.0, "44.1 kHz" },
        { 48000.0, "48 kHz"   },
        { 88200.0, "88.2 kHz" },
        { 96000.0, "96 kHz"   },
    };

    juce::PopupMenu menu;
    int id = 1;
    for (const auto& opt : options)
    {
        const bool isCurrent = std::abs (opt.rate - currentRate) < 1.0;
        menu.addItem (id++, opt.label, true, isCurrent);
    }

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
            sampleRateLabel.getScreenBounds()),
        [this] (int result)
        {
            if (result < 1 || result > 4) return;

            const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
            const double chosenRate = rates[result - 1];

            auto& deviceManager = appState.audioEngine.getDeviceManager();
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.sampleRate = chosenRate;
            deviceManager.setAudioDeviceSetup (setup, true);

            // Update label immediately — timerCallback will sync in 2s otherwise
            const juce::String srText = juce::String (chosenRate / 1000.0, 1) + " kHz";
            sampleRateLabel.setText (srText, juce::dontSendNotification);
        });
}
