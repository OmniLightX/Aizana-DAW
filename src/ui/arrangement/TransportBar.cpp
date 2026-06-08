#include "TransportBar.h"

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

    // ---- Time signature controls ----
    timeSigNumerator.addItem ("2",  1);
    timeSigNumerator.addItem ("3",  2);
    timeSigNumerator.addItem ("4",  3);
    timeSigNumerator.addItem ("5",  4);
    timeSigNumerator.addItem ("6",  5);
    timeSigNumerator.addItem ("7",  6);
    timeSigNumerator.addItem ("8",  7);
    timeSigNumerator.addItem ("9",  8);
    timeSigNumerator.addItem ("12", 9);
    timeSigNumerator.setSelectedId (3, juce::dontSendNotification);  // default 4/4

    timeSigDenominator.addItem ("2",  1);
    timeSigDenominator.addItem ("4",  2);
    timeSigDenominator.addItem ("8",  3);
    timeSigDenominator.addItem ("16", 4);
    timeSigDenominator.setSelectedId (2, juce::dontSendNotification);  // default 4/4

    // Shared handler: build "num/denom" string and call setStringTimeSig
    auto applyTimeSig = [this]
    {
        auto* edit = appState.editManager.getEdit();
        if (edit == nullptr) return;

        const int numItems[] = { 2, 3, 4, 5, 6, 7, 8, 9, 12 };
        const int denItems[] = { 2, 4, 8, 16 };

        const int numIdx = timeSigNumerator.getSelectedId() - 1;
        const int denIdx = timeSigDenominator.getSelectedId() - 1;
        if (numIdx < 0 || numIdx >= 9 || denIdx < 0 || denIdx >= 4)
            return;

        const int newNum = numItems[numIdx];
        const int newDen = denItems[denIdx];

        auto& seq = edit->tempoSequence;
        if (seq.getNumTimeSigs() > 0)
        {
            auto& undoManager = appState.editManager.getUndoManager();
            undoManager.beginNewTransaction ("Set time signature");
            seq.getTimeSig (0)->setStringTimeSig (
                juce::String (newNum) + "/" + juce::String (newDen));
        }

        // Keep metronome beats per bar in sync
        appState.audioEngine.getMetronome().setBeatsPerBar (newNum);
    };

    timeSigNumerator.onChange   = applyTimeSig;
    timeSigDenominator.onChange = applyTimeSig;

    timeSigSlash.setText ("/", juce::dontSendNotification);
    timeSigSlash.setJustificationType (juce::Justification::centred);
    timeSigSlash.setFont (juce::FontOptions (14.0f));
    timeSigSlash.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

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

    // Pre-Count / Vorzähler — N bars of metronome before recording drops in.
    precountButton.setClickingTogglesState (false);   // invariant I-04: drive state ourselves
    precountButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a3a));
    precountButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2d4a6a));  // blue when active
    precountButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff9aa0a6));
    precountButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
    precountButton.setTooltip ("Vorzähler vor der Aufnahme (Aus / 1 Takt / 2 Takte) — Metronom muss an sein");
    precountButton.onClick = [this] { cyclePreCount(); };
    updatePreCountButton();

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

    addAndMakeVisible (rewindButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (recordButton);
    addAndMakeVisible (metronomeButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (tapTempoButton);
    addAndMakeVisible (precountButton);
    addAndMakeVisible (modeButton);
    addAndMakeVisible (assistButton);
    addAndMakeVisible (positionLabel);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (timeSigNumerator);
    addAndMakeVisible (timeSigSlash);
    addAndMakeVisible (timeSigDenominator);
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
    place (metronomeButton, 3);
    place (loopButton,   3);
    place (tapTempoButton, 6);
    precountButton.setBounds (area.removeFromLeft (66).withHeight (buttonH));

    // Right side: Dial (mode + assist) → sample rate → BPM → time sig (rightmost to leftmost)
    modeButton.setBounds   (area.removeFromRight (104).withHeight (buttonH));
    area.removeFromRight (4);
    assistButton.setBounds (area.removeFromRight (62).withHeight (buttonH));
    area.removeFromRight (10);
    sampleRateLabel.setBounds    (area.removeFromRight (70));
    area.removeFromRight (4);
    bpmLabel.setBounds           (area.removeFromRight (100));
    area.removeFromRight (4);
    // Time signature: den / slash / num (right to left) — visually "4 / 4" left of BPM
    timeSigDenominator.setBounds (area.removeFromRight (36).withHeight (buttonH));
    area.removeFromRight (2);
    timeSigSlash.setBounds       (area.removeFromRight (10).withHeight (buttonH));
    area.removeFromRight (2);
    timeSigNumerator.setBounds   (area.removeFromRight (36).withHeight (buttonH));
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

    // Keep BPM label and time signature combos in sync (handles undo/redo changes)
    if (! bpmLabel.isBeingEdited())
    {
        updateBpmLabel();
        updateTimeSigDisplay();
        updatePreCountButton();

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

    // Map numerator value to combo ID (items: 2,3,4,5,6,7,8,9,12)
    const int numItems[] = { 2, 3, 4, 5, 6, 7, 8, 9, 12 };
    for (int i = 0; i < 9; ++i)
        if (numItems[i] == num)
        {
            timeSigNumerator.setSelectedId (i + 1, juce::dontSendNotification);
            break;
        }

    // Map denominator value to combo ID (items: 2, 4, 8, 16)
    const int denItems[] = { 2, 4, 8, 16 };
    for (int i = 0; i < 4; ++i)
        if (denItems[i] == den)
        {
            timeSigDenominator.setSelectedId (i + 1, juce::dontSendNotification);
            break;
        }
}

void TransportBar::cyclePreCount()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    using CI = tracktion::engine::Edit::CountIn;
    const auto cur  = edit->getCountInMode();
    const auto next = (cur == CI::none)   ? CI::oneBar
                    : (cur == CI::oneBar) ? CI::twoBar
                                          : CI::none;
    edit->setCountInMode (next);
    updatePreCountButton();
}

void TransportBar::updatePreCountButton()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    using CI = tracktion::engine::Edit::CountIn;
    const auto m = edit->getCountInMode();
    precountButton.setButtonText (m == CI::oneBar ? "Vorz: 1T"
                                 : m == CI::twoBar ? "Vorz: 2T"
                                                   : "Vorz: Aus");
    precountButton.setToggleState (m != CI::none, juce::dontSendNotification);
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
        transport.stop (false, false);
    else
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
