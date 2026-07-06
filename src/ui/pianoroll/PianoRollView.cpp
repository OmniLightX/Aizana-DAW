#include "PianoRollView.h"
#include "VelocityLane.h"
#include "CCLane.h"
#include "engine/EditManager.h"
#include "engine/ClipEditor.h"

//==============================================================================
// PianoKeyboard sub-component
//==============================================================================

static bool isMidiBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

static juce::String midiNoteName (int pitch)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int octave = (pitch / 12) - 1;
    return juce::String (names[pitch % 12]) + juce::String (octave);
}

void PianoRollView::PianoKeyboard::paint (juce::Graphics& g)
{
    const auto  bounds = getLocalBounds();
    const int   h      = bounds.getHeight();
    const double rowH  = owner.rowHeightPx();

    // Draw white keys first, then black keys on top
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        const float y    = owner.yAtPitch (pitch);
        const float rowBottom = y + static_cast<float> (rowH);
        if (rowBottom < 0.0f || y > static_cast<float> (h))
            continue;

        if (!isMidiBlackKey (pitch))
        {
            g.setColour (juce::Colours::white);
            g.fillRect (juce::Rectangle<float> (0.0f, y, static_cast<float> (getWidth()) - 1.0f,
                                                static_cast<float> (rowH)));
            g.setColour (juce::Colour (0xffcccccc));
            g.drawLine (0.0f, rowBottom, static_cast<float> (getWidth()), rowBottom, 0.5f);

            // Draw note name for C notes
            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colours::darkgrey);
                g.setFont (juce::FontOptions (9.0f));
                g.drawText (midiNoteName (pitch),
                            2, static_cast<int> (y),
                            getWidth() - 4, static_cast<int> (rowH),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    // Black keys (drawn on top)
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        if (!isMidiBlackKey (pitch))
            continue;

        const float y = owner.yAtPitch (pitch);
        const float rowBottom = y + static_cast<float> (rowH);
        if (rowBottom < 0.0f || y > static_cast<float> (h))
            continue;

        g.setColour (juce::Colour (0xff333333));
        g.fillRect (juce::Rectangle<float> (0.0f, y,
                                            static_cast<float> (getWidth()) * 0.65f,
                                            static_cast<float> (rowH)));
    }

    // Right border
    g.setColour (juce::Colour (0xff555555));
    g.drawLine (static_cast<float> (getWidth()) - 0.5f, 0.0f,
                static_cast<float> (getWidth()) - 0.5f, static_cast<float> (h), 1.0f);

    // Highlight the actively-pressed key in blue
    if (activePreviewNote >= 0)
    {
        const float ky = owner.yAtPitch (activePreviewNote);
        const float kh = static_cast<float> (owner.rowHeightPx());
        g.setColour (juce::Colour (0xff4488ff).withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (0.0f, ky,
                                            static_cast<float> (getWidth()) - 1.0f, kh));
    }
}

//==============================================================================
// PianoKeyboard mouse / MIDI preview
//==============================================================================

int PianoRollView::PianoKeyboard::pitchFromY (float y) const noexcept
{
    // owner.pitchAtY maps a y-coordinate (relative to PianoRollView) to a MIDI pitch.
    // PianoKeyboard is a direct child of PianoRollView with identical y-offset, so
    // local y == PianoRollView-local y for the keyboard strip.
    return juce::jlimit (0, 127, owner.pitchAtY (y));
}

void PianoRollView::PianoKeyboard::startPreviewNote (int pitch)
{
    if (pitch == activePreviewNote) return;
    stopPreviewNote();
    activePreviewNote = pitch;
    owner.sendPreviewNoteOn (pitch, 100);
    repaint();
}

void PianoRollView::PianoKeyboard::stopPreviewNote()
{
    if (activePreviewNote >= 0)
    {
        owner.sendPreviewNoteOff (activePreviewNote);
        activePreviewNote = -1;
        repaint();
    }
}

void PianoRollView::PianoKeyboard::mouseDown (const juce::MouseEvent& e)
{
    startPreviewNote (pitchFromY (static_cast<float> (e.y)));
}

void PianoRollView::PianoKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    startPreviewNote (pitchFromY (static_cast<float> (e.y)));
}

void PianoRollView::PianoKeyboard::mouseUp (const juce::MouseEvent&)
{
    stopPreviewNote();
}

//==============================================================================
// PianoRollView MIDI preview helpers
//==============================================================================

void PianoRollView::sendPreviewNoteOn (int pitch, int velocity)
{
    // Inject a MIDI note-on via the engine's device manager.
    // This sends to all active MIDI output devices so the instrument
    // assigned to the track hears the preview note.
    // Approach: engine DeviceManager → all MIDI output devices (TE v3.2.0 path).
    auto msg = juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (velocity));
    auto& deviceManager = clip.edit.engine.getDeviceManager();
    for (int i = 0; i < deviceManager.getNumMidiOutDevices(); ++i)
        if (auto* dev = deviceManager.getMidiOutDevice (i))
            if (dev->isEnabled())
                dev->fireMessage (msg);
}

void PianoRollView::sendPreviewNoteOff (int pitch)
{
    auto msg = juce::MidiMessage::noteOff (1, pitch, static_cast<juce::uint8> (0));
    auto& deviceManager = clip.edit.engine.getDeviceManager();
    for (int i = 0; i < deviceManager.getNumMidiOutDevices(); ++i)
        if (auto* dev = deviceManager.getMidiOutDevice (i))
            if (dev->isEnabled())
                dev->fireMessage (msg);
}

//==============================================================================
// TakesPanel
//==============================================================================

PianoRollView::TakesPanel::TakesPanel (te::MidiClip& clip_, EditManager& editManager_)
    : clip (clip_), editManager (editManager_)
{
    rebuild();
}

void PianoRollView::TakesPanel::rebuild()
{
    takeButtons.clear();
    const int numTakes = clip.getNumTakes (false);

    for (int i = 0; i < numTakes; ++i)
    {
        auto* btn = takeButtons.add (new juce::TextButton ("Take " + juce::String (i + 1)));
        const int currentTake = clip.getCurrentTake();
        btn->setColour (juce::TextButton::buttonColourId,
                        i == currentTake
                            ? juce::Colour (0xff1fa882)
                            : juce::Colour (0xff3a3a3a));
        const int takeIndex = i;
        btn->onClick = [this, takeIndex]
        {
            editManager.getUndoManager().beginNewTransaction();
            clip.setCurrentTake (takeIndex);
            rebuild();
            repaint();
        };
        addAndMakeVisible (btn);
    }
    resized();
}

void PianoRollView::TakesPanel::resized()
{
    const int btnW = 80;
    int x = 0;
    for (auto* btn : takeButtons)
    {
        btn->setBounds (x, 0, btnW, getHeight());
        x += btnW + 2;
    }
}

void PianoRollView::TakesPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
    g.setColour (juce::Colour (0xff444444));
    g.drawLine (0.0f, 0.0f, static_cast<float> (getWidth()), 0.0f, 1.0f);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("TAKES", 2, 0, 50, getHeight(), juce::Justification::centredLeft, false);
}

void PianoRollView::TakesPanel::mouseDown (const juce::MouseEvent&) {}

//==============================================================================
// PianoRollView
//==============================================================================

PianoRollView::PianoRollView (te::MidiClip& clip_, EditManager& editManager_)
    : clip (clip_), editManager (editManager_)
{
    // ---- Header controls ----
    // Key selector: C through B
    keySelector.addItem ("C",  1);
    keySelector.addItem ("C#", 2);
    keySelector.addItem ("D",  3);
    keySelector.addItem ("D#", 4);
    keySelector.addItem ("E",  5);
    keySelector.addItem ("F",  6);
    keySelector.addItem ("F#", 7);
    keySelector.addItem ("G",  8);
    keySelector.addItem ("G#", 9);
    keySelector.addItem ("A",  10);
    keySelector.addItem ("A#", 11);
    keySelector.addItem ("B",  12);
    keySelector.setSelectedId (1);
    keySelector.onChange = [this]
    {
        selectedKey = keySelector.getSelectedId() - 1;
        updateAllNoteScaleColors();
        repaint();
    };
    addAndMakeVisible (keySelector);

    // Scale selector
    scaleSelector.addItem ("Major",       1);
    scaleSelector.addItem ("Minor",       2);
    scaleSelector.addItem ("Dorian",      3);
    scaleSelector.addItem ("Mixolydian",  4);
    scaleSelector.addItem ("Pentatonic",  5);
    scaleSelector.addItem ("Chromatic",   6);
    scaleSelector.setSelectedId (1);
    scaleSelector.onChange = [this]
    {
        const int id = scaleSelector.getSelectedId();
        if      (id == 1) scaleIntervals = {0,2,4,5,7,9,11};         // Major
        else if (id == 2) scaleIntervals = {0,2,3,5,7,8,10};         // Minor
        else if (id == 3) scaleIntervals = {0,2,3,5,7,9,10};         // Dorian
        else if (id == 4) scaleIntervals = {0,2,4,5,7,9,10};         // Mixolydian
        else if (id == 5) scaleIntervals = {0,2,4,7,9};              // Pentatonic
        else              scaleIntervals = {0,1,2,3,4,5,6,7,8,9,10,11}; // Chromatic

        updateAllNoteScaleColors();
        repaint();
    };
    addAndMakeVisible (scaleSelector);

    // Quantize grid
    quantizeGrid.addItem ("1/4",  1);
    quantizeGrid.addItem ("1/8",  2);
    quantizeGrid.addItem ("1/16", 3);
    quantizeGrid.addItem ("1/32", 4);
    quantizeGrid.setSelectedId (3);  // default 1/16
    addAndMakeVisible (quantizeGrid);

    // ---- Quantize-Bar (E7): Strength/Swing + Apply — S1 "Exploring Quantisation" ----
    auto setupQSlider = [this] (juce::Slider& s, double def, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 16);
        s.setRange (0.0, 100.0, 1.0);
        s.setValue (def, juce::dontSendNotification);
        s.setTextValueSuffix ("%");
        s.setTooltip (tip);
        addAndMakeVisible (s);
    };
    setupQSlider (qStrength, 100.0,
                  juce::String::fromUTF8 ("Quantize-St\xc3\xa4rke — 100 % = voll aufs Raster, "
                                          "50 % = halber Weg (nat\xc3\xbcrlicher)"));
    setupQSlider (qSwing, 0.0,
                  juce::String::fromUTF8 ("Swing — verschiebt jede zweite Rasterlinie nach hinten"));

    qApplyBtn.setClickingTogglesState (false);   // I-04
    qApplyBtn.setTooltip (juce::String::fromUTF8 ("Quantisieren (Q) — selektierte Noten, sonst alle; 1 Undo-Schritt"));
    qApplyBtn.onClick = [this] { applyQuantize(); };
    addAndMakeVisible (qApplyBtn);

    // ---- Sub-lanes ----
    velocityLane = std::make_unique<VelocityLane> (clip, *this, editManager);
    addAndMakeVisible (*velocityLane);

    ccLane = std::make_unique<CCLane> (clip, 7, *this, editManager);  // CC7 Volume
    addAndMakeVisible (*ccLane);

    // Takes panel — only show if clip has multiple takes
    if (clip.getNumTakes (false) > 1)
    {
        takesPanel = std::make_unique<TakesPanel> (clip, editManager);
        addAndMakeVisible (*takesPanel);
    }

    // ---- Scroll bars ----
    hScrollBar.addListener (this);
    vScrollBar.addListener (this);
    addAndMakeVisible (hScrollBar);
    addAndMakeVisible (vScrollBar);

    // ---- Piano keyboard ----
    addAndMakeVisible (pianoKeyboard);

    // ---- Listen to MidiClip ValueTree for note changes ----
    clip.state.addListener (this);

    // ---- Build initial note components ----
    rebuildNoteComponents();

    setSize (900, 500);
}

PianoRollView::~PianoRollView()
{
    clip.state.removeListener (this);
}

//==============================================================================

void PianoRollView::resized()
{
    const auto bounds = getLocalBounds();
    const int w = bounds.getWidth();
    const int h = bounds.getHeight();

    static constexpr int kScrollBarThickness = 12;
    static constexpr int kVelocityLaneH      = 80;
    static constexpr int kCCLaneH            = 60;
    static constexpr int kTakesH             = 30;

    // Determine how much height takes panel needs
    const int takesH = (takesPanel != nullptr) ? kTakesH : 0;

    // Header row
    const int headerH = kHeaderHeight;
    {
        int x = kKeyboardWidth;
        const int ctrlW = 90;
        keySelector.setBounds   (x, 2, ctrlW, headerH - 4);   x += ctrlW + 4;
        scaleSelector.setBounds (x, 2, ctrlW + 20, headerH - 4); x += ctrlW + 24;
        quantizeGrid.setBounds  (x, 2, 70, headerH - 4);       x += 74;

        // Quantize-Bar (E7): Strength | Swing | Q
        qStrength.setBounds (x, 2, 120, headerH - 4);          x += 124;
        qSwing.setBounds    (x, 2, 120, headerH - 4);          x += 124;
        qApplyBtn.setBounds (x, 2, 28, headerH - 4);
    }

    // Calculate grid area height
    const int lanesH = kVelocityLaneH + kCCLaneH + takesH + kScrollBarThickness;
    const int gridH  = h - headerH - lanesH;

    // Piano keyboard
    pianoKeyboard.setBounds (0, headerH, kKeyboardWidth, gridH);

    // Vertical scroll bar (right edge of grid area)
    vScrollBar.setBounds (w - kScrollBarThickness, headerH, kScrollBarThickness, gridH);
    vScrollBar.setRangeLimits (0.0, kTotalPitches * rowH);
    vScrollBar.setCurrentRange (scrollBeat * rowH, gridH);

    // Horizontal scroll bar (bottom of entire content area)
    const int hScrollY = h - kScrollBarThickness;
    hScrollBar.setBounds (kKeyboardWidth, hScrollY, w - kKeyboardWidth - kScrollBarThickness, kScrollBarThickness);
    const double totalBeats = clip.getLengthInBeats().inBeats() + 8.0;
    hScrollBar.setRangeLimits (0.0, totalBeats * ppb);
    hScrollBar.setCurrentRange (scrollBeat * ppb, (w - kKeyboardWidth - kScrollBarThickness));

    // Sub-lanes
    int laneY = headerH + gridH;
    velocityLane->setBounds (kKeyboardWidth, laneY, w - kKeyboardWidth - kScrollBarThickness, kVelocityLaneH);
    laneY += kVelocityLaneH;

    ccLane->setBounds (kKeyboardWidth, laneY, w - kKeyboardWidth - kScrollBarThickness, kCCLaneH);
    laneY += kCCLaneH;

    if (takesPanel != nullptr)
    {
        takesPanel->setBounds (kKeyboardWidth, laneY, w - kKeyboardWidth - kScrollBarThickness, kTakesH);
    }

    // Position all NoteComponents
    for (auto* nc : noteComponents)
        updateNoteComponentBounds (*nc);
}

//==============================================================================

void PianoRollView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const int w = bounds.getWidth();
    const int h = bounds.getHeight();

    static constexpr int kVelocityLaneH = 80;
    static constexpr int kCCLaneH       = 60;
    static constexpr int kScrollBarThick = 12;
    const int takesH = (takesPanel != nullptr) ? 30 : 0;
    const int lanesH = kVelocityLaneH + kCCLaneH + takesH + kScrollBarThick;
    const int gridH  = h - kHeaderHeight - lanesH;
    const int gridW  = w - kKeyboardWidth - kScrollBarThick;

    // ---- Background ----
    g.fillAll (juce::Colour (0xff1e1e1e));

    // ---- Header background ----
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRect (0, 0, w, kHeaderHeight);
    g.setColour (juce::Colour (0xff444444));
    g.drawLine (0.0f, static_cast<float> (kHeaderHeight),
                static_cast<float> (w), static_cast<float> (kHeaderHeight), 1.0f);

    // ---- Beat marker header ----
    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (10.0f));
    const int firstBeat = static_cast<int> (scrollBeat);
    const int lastBeat  = firstBeat + static_cast<int> ((gridW) / ppb) + 2;
    for (int beat = firstBeat; beat <= lastBeat; ++beat)
    {
        const float x = xAtBeat (beat);
        if (x < kKeyboardWidth || x > w - kScrollBarThick)
            continue;

        const bool isBar = (beat % 4 == 0);
        g.setColour (isBar ? juce::Colours::white : juce::Colours::grey);
        g.drawLine (x, static_cast<float> (kHeaderHeight) - 8.0f,
                    x, static_cast<float> (kHeaderHeight), 1.0f);

        if (isBar)
        {
            g.setColour (juce::Colours::lightgrey);
            g.drawText (juce::String (beat / 4 + 1),
                        static_cast<int> (x) + 2, 2,
                        40, kHeaderHeight - 4,
                        juce::Justification::centredLeft, false);
        }
    }

    // ---- Clip the grid drawing to the grid area ----
    {
        juce::Graphics::ScopedSaveState savedState (g);
        g.reduceClipRegion (kKeyboardWidth, kHeaderHeight, gridW, gridH);

        // ---- Row backgrounds: alternate white/black key colours ----
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            const float y     = yAtPitch (pitch);
            const float rowBottom = y + static_cast<float> (rowH);
            if (rowBottom < kHeaderHeight || y > kHeaderHeight + gridH)
                continue;

            // Choose base row colour (black key rows are darker)
            const juce::Colour baseRow = isMidiBlackKey (pitch)
                ? juce::Colour (0xff1a1a1a)
                : juce::Colour (0xff252525);

            // In-scale rows get a slight teal tint
            const juce::Colour rowColour = isInScale (pitch)
                ? baseRow.interpolatedWith (juce::Colour (0xff1fa882), 0.08f)
                : baseRow;

            g.setColour (rowColour);
            g.fillRect (juce::Rectangle<float> (static_cast<float> (kKeyboardWidth), y,
                                                static_cast<float> (gridW), static_cast<float> (rowH)));

            // Row separator line
            g.setColour (juce::Colour (0xff333333));
            g.drawLine (static_cast<float> (kKeyboardWidth), rowBottom,
                        static_cast<float> (w - kScrollBarThick), rowBottom, 0.5f);
        }

        // ---- Vertical beat/bar lines ----
        for (int beat = firstBeat; beat <= lastBeat; ++beat)
        {
            const float x = xAtBeat (beat);
            if (x < kKeyboardWidth || x > w - kScrollBarThick)
                continue;

            const bool isBar = (beat % 4 == 0);
            g.setColour (isBar
                ? juce::Colour (0xff555555)
                : juce::Colour (0xff333333));
            g.drawLine (x, static_cast<float> (kHeaderHeight),
                        x, static_cast<float> (kHeaderHeight + gridH), 0.5f);
        }

        // ---- Quantize grid sub-beat lines ----
        const int div = getGridDivisionDenominator();
        if (div > 4)
        {
            const double subBeatStep = 4.0 / div;
            for (double b = firstBeat; b < lastBeat; b += subBeatStep)
            {
                if (std::fmod (b, 1.0) < 0.001) continue; // skip beat lines already drawn
                const float x = xAtBeat (b);
                if (x < kKeyboardWidth || x > w - kScrollBarThick)
                    continue;
                g.setColour (juce::Colour (0xff2a2a2a));
                g.drawLine (x, static_cast<float> (kHeaderHeight),
                            x, static_cast<float> (kHeaderHeight + gridH), 0.5f);
            }
        }

        // ---- C octave separator lines (thicker) ----
        for (int pitch = 0; pitch < 128; pitch += 12)
        {
            const float y = yAtPitch (pitch);
            g.setColour (juce::Colour (0xff555555));
            g.drawLine (static_cast<float> (kKeyboardWidth), y,
                        static_cast<float> (w - kScrollBarThick), y, 1.0f);
        }
    }

    // ---- Marquee rubber-band selection rectangle ----
    if (isMarqueeActive)
    {
        g.setColour (juce::Colour (0x334488ff));
        g.fillRect (marqueeRect);
        g.setColour (juce::Colour (0xff4488ff));
        g.drawRect (marqueeRect, 1.0f);
    }
}

//==============================================================================
// Coordinate helpers
//==============================================================================

float PianoRollView::xAtBeat (double beat) const noexcept
{
    return static_cast<float> (kKeyboardWidth + (beat - scrollBeat) * ppb);
}

float PianoRollView::yAtPitch (int pitch) const noexcept
{
    // Higher pitches are at top; MIDI 127 = top, 0 = bottom
    const int relativePitch = scrollPitch - pitch;
    return static_cast<float> (kHeaderHeight + relativePitch * rowH);
}

double PianoRollView::beatAtX (float x) const noexcept
{
    return scrollBeat + (x - kKeyboardWidth) / ppb;
}

int PianoRollView::pitchAtY (float y) const noexcept
{
    const double relative = (y - kHeaderHeight) / rowH;
    return juce::jlimit (0, 127, scrollPitch - static_cast<int> (relative));
}

bool PianoRollView::isInScale (int pitch) const noexcept
{
    const int pc = ((pitch % 12) - selectedKey + 12) % 12;
    return scaleIntervals.contains (pc);
}

//==============================================================================
// Zoom
//==============================================================================

void PianoRollView::setHorizontalZoom (double pixelsPerBeat_)
{
    ppb = juce::jlimit (10.0, 400.0, pixelsPerBeat_);
    resized();
    repaint();
    velocityLane->repaintLane();
    ccLane->repaintLane();
}

void PianoRollView::setVerticalZoom (double rowHeightPixels)
{
    rowH = juce::jlimit (4.0, 40.0, rowHeightPixels);
    resized();
    repaint();
}

//==============================================================================
// Scale/key locking
//==============================================================================

void PianoRollView::setKey (int rootNote, const juce::Array<int>& intervals)
{
    selectedKey    = rootNote;
    scaleIntervals = intervals;
    keySelector.setSelectedId (rootNote + 1, juce::dontSendNotification);
    updateAllNoteScaleColors();
    repaint();
}

void PianoRollView::setKeyLockEnabled (bool prevent)
{
    keyLockEnabled = prevent;
}

//==============================================================================
// Note building / helpers
//==============================================================================

int PianoRollView::getGridDivisionDenominator() const noexcept
{
    switch (quantizeGrid.getSelectedId())
    {
        case 1:  return 4;
        case 2:  return 8;
        case 3:  return 16;
        case 4:  return 32;
        default: return 16;
    }
}

double PianoRollView::quantizeBeat (double rawBeat) const noexcept
{
    const int div = getGridDivisionDenominator();
    const double step = 4.0 / div;  // beats per grid step (assuming 4/4)
    return std::round (rawBeat / step) * step;
}

int PianoRollView::snapToScale (int pitch) const noexcept
{
    if (isInScale (pitch))
        return pitch;

    // Find nearest in-scale pitch (try up and down within an octave)
    for (int delta = 1; delta <= 12; ++delta)
    {
        if (pitch + delta <= 127 && isInScale (pitch + delta)) return pitch + delta;
        if (pitch - delta >= 0   && isInScale (pitch - delta)) return pitch - delta;
    }
    return pitch; // Fallback (shouldn't happen for non-chromatic scales)
}

void PianoRollView::addNote (double beatPosition, int pitch, float velocity)
{
    double snappedBeat = quantizeBeat (beatPosition);
    int    snappedPitch = pitch;

    if (keyLockEnabled)
        snappedPitch = snapToScale (pitch);

    const double defaultLengthBeats = 4.0 / getGridDivisionDenominator();

    auto& midiList = clip.getSequence();
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction();

    // Use the Tracktion Engine MidiList API
    // addNote(pitch, startBeat, lengthInBeats, velocity, colour, undoManager)
    midiList.addNote (snappedPitch,
                      tracktion::core::BeatPosition::fromBeats (snappedBeat),
                      tracktion::core::BeatDuration::fromBeats (defaultLengthBeats),
                      juce::jlimit (1, 127, static_cast<int> (velocity * 127.0f)),
                      0, &undoManager);
}

void PianoRollView::removeNote (te::MidiNote& note)
{
    auto& midiList = clip.getSequence();
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction();
    midiList.removeNote (note, &undoManager);
}

void PianoRollView::moveNote (te::MidiNote& note, int newPitch, double newStartBeat)
{
    auto& undoManager = editManager.getUndoManager();

    // Update pitch
    if (note.getNoteNumber() != newPitch)
        note.setNoteNumber (newPitch, &undoManager);

    // Update start position (keep existing length)
    note.setStartAndLength (tracktion::core::BeatPosition::fromBeats (newStartBeat),
                            note.getLengthBeats(),
                            &undoManager);
}

void PianoRollView::resizeNote (te::MidiNote& note, double newLengthBeats)
{
    auto& undoManager = editManager.getUndoManager();

    note.setStartAndLength (note.getStartBeat(),
                            tracktion::core::BeatDuration::fromBeats (newLengthBeats),
                            &undoManager);
}

//==============================================================================
// Selection
//==============================================================================

void PianoRollView::selectAll()
{
    for (auto* nc : noteComponents)
        nc->setSelected (true);
}

void PianoRollView::deselectAll()
{
    for (auto* nc : noteComponents)
        nc->setSelected (false);
    isMarqueeActive = false;
    repaint();
}

void PianoRollView::deleteSelectedNotes()
{
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Delete Selected Notes");

    // Collect notes to delete first to avoid iterator invalidation
    juce::Array<te::MidiNote*> toDelete;
    for (auto* nc : noteComponents)
        if (nc->isSelected())
            toDelete.add (&nc->getNote());

    for (auto* n : toDelete)
        removeNote (*n);
}

//==============================================================================
// Quantize-Bar (E7) — selektierte Noten, sonst alle; 1 Undo über ClipEditor
//==============================================================================
void PianoRollView::applyQuantize()
{
    const double grid     = 4.0 / getGridDivisionDenominator();   // beats (4/4)
    const double strength = qStrength.getValue() / 100.0;
    const double swing    = qSwing.getValue()    / 100.0;

    juce::Array<te::MidiNote*> selected;
    for (auto* nc : noteComponents)
        if (nc->isSelected())
            selected.add (&nc->getNote());

    ClipEditor ce (editManager);
    ce.quantizeMidiClip (clip, grid, strength, swing,
                         selected.isEmpty() ? nullptr : &selected);

    // Note-Positionen haben sich geändert — Components nachziehen.
    for (auto* nc : noteComponents)
        updateNoteComponentBounds (*nc);
    repaint();
    if (velocityLane != nullptr) velocityLane->repaintLane();
    if (ccLane != nullptr)       ccLane->repaintLane();
}

void PianoRollView::updateMarqueeSelection()
{
    for (auto* nc : noteComponents)
    {
        const bool intersects = marqueeRect.intersects (nc->getBounds().toFloat());
        nc->setSelected (intersects);
    }
}

//==============================================================================
// Rebuild NoteComponents from MidiList
//==============================================================================

void PianoRollView::rebuildNoteComponents()
{
    noteComponents.clear();

    auto& midiList = clip.getSequence();
    const int numNotes = midiList.getNumNotes();

    for (int i = 0; i < numNotes; ++i)
    {
        if (auto* note = midiList.getNote (i))
        {
            auto* nc = noteComponents.add (new NoteComponent (*note, *this, editManager));
            nc->setInScale (isInScale (note->getNoteNumber()));
            nc->onSelectionRequest = [this] (NoteComponent* clicked)
            {
                deselectAll();
                clicked->setSelected (true);
            };
            addAndMakeVisible (nc);
            updateNoteComponentBounds (*nc);
        }
    }
}

void PianoRollView::updateNoteComponentBounds (NoteComponent& nc)
{
    const auto& note = nc.getNote();
    const float x    = xAtBeat (note.getStartBeat().inBeats());
    const float y    = yAtPitch (note.getNoteNumber());
    const float w    = static_cast<float> (note.getLengthBeats().inBeats() * ppb);
    const float h    = static_cast<float> (rowH);

    nc.setBounds (static_cast<int> (x), static_cast<int> (y),
                  juce::jmax (4, static_cast<int> (w)), static_cast<int> (h));
}

void PianoRollView::updateAllNoteScaleColors()
{
    for (auto* nc : noteComponents)
        nc->setInScale (isInScale (nc->getNote().getNoteNumber()));
}

//==============================================================================
// Mouse input
//==============================================================================

void PianoRollView::mouseDown (const juce::MouseEvent& e)
{
    // Only handle left-click in the grid area (not on keyboard or header)
    if (e.x < kKeyboardWidth || e.y < kHeaderHeight)
        return;

    if (e.mods.isRightButtonDown())
        return; // NoteComponent handles right-click for deletes

    if (!e.mods.isLeftButtonDown())
        return;

    // Click on empty grid space: deselect all notes and prepare marquee
    deselectAll();
    marqueeStart = e.position;
    isMarqueeActive = false;

    // Also add a note (existing draw behaviour — NoteComponent will intercept if a note is clicked)
    const double beat  = beatAtX (static_cast<float> (e.x));
    const int    pitch = pitchAtY (static_cast<float> (e.y));
    addNote (beat, pitch);

    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.x < kKeyboardWidth || e.y < kHeaderHeight)
        return;

    if (!e.mods.isLeftButtonDown())
        return;

    if (!isMarqueeActive && e.position.getDistanceFrom (marqueeStart) > 4.0f)
        isMarqueeActive = true;

    if (isMarqueeActive)
    {
        marqueeRect = juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (marqueeStart.x, e.position.x),
            juce::jmin (marqueeStart.y, e.position.y),
            juce::jmax (marqueeStart.x, e.position.x),
            juce::jmax (marqueeStart.y, e.position.y));
        updateMarqueeSelection();
        repaint();
    }
}

void PianoRollView::mouseUp (const juce::MouseEvent&)
{
    isMarqueeActive = false;
    repaint();
}

bool PianoRollView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress (juce::KeyPress::deleteKey)
     || key == juce::KeyPress (juce::KeyPress::backspaceKey))
    {
        deleteSelectedNotes();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAll();
        return true;
    }
    if (key == juce::KeyPress ('q') || key == juce::KeyPress ('Q'))
    {
        applyQuantize();   // E7: Quantize-Bar-Shortcut
        return true;
    }
    return false;
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent& e,
                                     const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // Ctrl+scroll = horizontal zoom
        setHorizontalZoom (ppb * (1.0 + wheel.deltaY * 0.2));
    }
    else if (e.mods.isShiftDown())
    {
        // Shift+scroll = vertical scroll (pitch)
        scrollPitch = juce::jlimit (0, 127,
                                    scrollPitch + static_cast<int> (wheel.deltaY * 3));
        repaint();
        pianoKeyboard.repaint();
        for (auto* nc : noteComponents)
            updateNoteComponentBounds (*nc);
    }
    else
    {
        // Normal scroll = horizontal scroll (beats)
        scrollBeat = juce::jmax (0.0, scrollBeat - wheel.deltaY * 2.0);
        repaint();
        pianoKeyboard.repaint();
        for (auto* nc : noteComponents)
            updateNoteComponentBounds (*nc);
        velocityLane->repaintLane();
        ccLane->repaintLane();
    }
}

void PianoRollView::scrollBarMoved (juce::ScrollBar* bar, double newScrollPos)
{
    if (bar == &hScrollBar)
    {
        scrollBeat = newScrollPos / ppb;
        repaint();
        for (auto* nc : noteComponents)
            updateNoteComponentBounds (*nc);
        velocityLane->repaintLane();
        ccLane->repaintLane();
    }
    else if (bar == &vScrollBar)
    {
        scrollPitch = static_cast<int> (newScrollPos / rowH);
        repaint();
        pianoKeyboard.repaint();
        for (auto* nc : noteComponents)
            updateNoteComponentBounds (*nc);
    }
}

//==============================================================================
// ValueTree::Listener
//==============================================================================

void PianoRollView::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    juce::MessageManager::callAsync ([this] { rebuildNoteComponents(); repaint(); });
}

void PianoRollView::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    juce::MessageManager::callAsync ([this] { rebuildNoteComponents(); repaint(); });
}
