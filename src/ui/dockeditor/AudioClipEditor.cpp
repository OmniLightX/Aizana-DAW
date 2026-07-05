#include "AudioClipEditor.h"

using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using TimeDur = tracktion::core::TimeDuration;

namespace
{
    const juce::Colour kAccent (0xff44aaff);

    void styleToolButton (juce::TextButton& b)
    {
        b.setClickingTogglesState (false);   // I-04: JUCE-Doppel-Toggle-Race verhindern
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a32));
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.85f));
    }
}

//==============================================================================
AudioClipEditor::AudioClipEditor (te::WaveAudioClip& clip_, EditManager& editManager_)
    : clip (clip_), editManager (editManager_),
      clipEditor (editManager_), automationEngine (editManager_)
{
    // Waveform source — shared app-wide thumbnail cache (finished reads survive
    // component destruction, so reopening the editor repaints instantly).
    const auto audioFile = clip.getSourceFileReference().getFile();
    if (audioFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (audioFile));

    nameLabel.setText (clip.getName(), juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (nameLabel);

    gainCaption.setText ("Gain", juce::dontSendNotification);
    gainCaption.setFont (juce::FontOptions (10.0f));
    gainCaption.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    gainCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (gainCaption);

    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    syncGainSlider();
    gainSlider.onDragStart = [this]
    {
        editManager.getUndoManager().beginNewTransaction ("Clip gain");
    };
    gainSlider.onValueChange = [this]
    {
        clip.state.setProperty (te::IDs::gain, gainSlider.getValue(),
                                &editManager.getUndoManager());
    };
    addAndMakeVisible (gainSlider);

    zoomInButton.onClick  = [this] { zoomAround (1.0 / 1.4, waveArea().getCentreX()); };
    zoomOutButton.onClick = [this] { zoomAround (1.4,       waveArea().getCentreX()); };
    zoomFitButton.onClick = [this]
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
        syncScrollBar();
        updateLaneMapping();
        repaint();
    };
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
    addAndMakeVisible (zoomFitButton);

    // ---- Toolbar (S1-Parität E1/E3): Werkzeuge, Snap, Mute, Automation, Range-Aktion ----
    for (auto* b : { &toolPointerBtn, &toolSplitBtn, &toolRangeBtn,
                     &snapBtn, &muteBtn, &autoBtn, &rangeCutBtn })
    {
        styleToolButton (*b);
        addAndMakeVisible (*b);
    }

    toolPointerBtn.setTooltip (juce::String::fromUTF8 ("Pfeil-Werkzeug (1/Esc) — Trim, Fades, Seek"));
    toolSplitBtn  .setTooltip (juce::String::fromUTF8 ("Split-Werkzeug (2/C) — Klick schneidet; Doppelklick schneidet immer"));
    toolRangeBtn  .setTooltip (juce::String::fromUTF8 ("Range-Werkzeug (3, oder Cmd+Ziehen) — Bereich w\xc3\xa4hlen, Delete entfernt ihn"));
    snapBtn       .setTooltip (juce::String::fromUTF8 ("Snap an 1/16-Raster (N) — Alt = tempor\xc3\xa4r aus"));
    muteBtn       .setTooltip (juce::String::fromUTF8 ("Clip stummschalten (undobar)"));
    autoBtn       .setTooltip (juce::String::fromUTF8 ("Automations-Spur (Volume/Pan) ein-/ausblenden"));
    rangeCutBtn   .setTooltip (juce::String::fromUTF8 ("Gew\xc3\xa4hlten Bereich aus dem Clip entfernen (Delete) — 1 Undo-Schritt"));

    toolPointerBtn.onClick = [this] { setTool (Tool::pointer); };
    toolSplitBtn  .onClick = [this] { setTool (Tool::split); };
    toolRangeBtn  .onClick = [this] { setTool (Tool::range); };
    snapBtn.onClick = [this] { snapEnabled = ! snapEnabled; updateToolbarState(); };
    muteBtn.onClick = [this] { toggleClipMute(); };
    autoBtn.onClick = [this] { toggleAutomationLane(); };
    rangeCutBtn.onClick = [this] { applyRangeDelete(); };

    // Automation nur möglich, wenn der Clip auf einem AudioTrack liegt.
    autoBtn.setEnabled (dynamic_cast<te::AudioTrack*> (clip.getTrack()) != nullptr);

    updateToolbarState();

    hScroll.setAutoHide (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    setWantsKeyboardFocus (true);   // Tastatur: 1/2/3, C, N, Esc, Delete
    clip.state.addListener (this);
    startTimerHz (30);   // playhead
}

AudioClipEditor::~AudioClipEditor()
{
    clip.state.removeListener (this);
}

//==============================================================================
double AudioClipEditor::clipLenSecs() const
{
    return juce::jmax (0.001, clip.getPosition().getLength().inSeconds());
}

double AudioClipEditor::sourceLenSecs()
{
    const auto info = clip.getAudioFile().getInfo();
    if (info.sampleRate > 0.0)
        return (double) info.lengthInSamples / info.sampleRate;

    // No readable source — fall back to "no extension possible"
    return clipLenSecs() + clip.getPosition().getOffset().inSeconds();
}

juce::Rectangle<int> AudioClipEditor::waveArea() const
{
    auto r = getLocalBounds();
    r.removeFromTop (kHeaderH + kToolbarH);
    r.removeFromBottom (kScrollH);
    if (automationVisible)
        r.removeFromBottom (kAutoLaneH);
    return r;
}

double AudioClipEditor::xToLocalSecs (float x) const
{
    const auto wa = waveArea();
    if (wa.getWidth() <= 0 || viewLenSecs <= 0.0)
        return 0.0;
    return viewStartSecs + ((double) x - wa.getX()) / wa.getWidth() * viewLenSecs;
}

float AudioClipEditor::localSecsToX (double t) const
{
    const auto wa = waveArea();
    if (viewLenSecs <= 0.0)
        return (float) wa.getX();
    return (float) (wa.getX() + (t - viewStartSecs) / viewLenSecs * wa.getWidth());
}

void AudioClipEditor::clampView()
{
    // S1-Anker: das Fenster schnappt NICHT auf die Clip-Grenzen zusammen —
    // nach Split/Range-Delete bleibt die Zeitachse stehen (Überhang erlaubt),
    // es muss den Clip nur noch SCHNEIDEN (sonst Fit, s. EditorViewState.h).
    const double len = clipLenSecs();
    if (viewLenSecs <= 0.0)
    {
        viewStartSecs = 0.0;
        viewLenSecs   = len;
    }
    const auto w  = editorview::ensureUsable ({ viewStartSecs, viewLenSecs }, len);
    viewStartSecs = w.startLocal;
    viewLenSecs   = w.len;
}

void AudioClipEditor::zoomAround (double factor, float pivotX)
{
    const double pivotSecs = xToLocalSecs (pivotX);
    viewLenSecs = editorview::clampZoomLen (viewLenSecs * factor, clipLenSecs(), viewLenSecs);
    // Keep the time under the cursor stationary
    const auto wa = waveArea();
    const double frac = wa.getWidth() > 0 ? ((double) pivotX - wa.getX()) / wa.getWidth() : 0.5;
    viewStartSecs = pivotSecs - frac * viewLenSecs;
    clampView();
    syncScrollBar();
    updateLaneMapping();
    repaint();
}

void AudioClipEditor::syncScrollBar()
{
    // Scroll-Raum umfasst Clip UND aktuelles Fenster (Überhang nach Edits)
    const double len = clipLenSecs();
    const double lo  = juce::jmin (0.0, viewStartSecs);
    const double hi  = juce::jmax (len, viewStartSecs + viewLenSecs);
    hScroll.setRangeLimits ({ lo, hi }, juce::dontSendNotification);
    hScroll.setCurrentRange ({ viewStartSecs, viewLenSecs }, juce::dontSendNotification);
}

void AudioClipEditor::syncGainSlider()
{
    const double db = (double) clip.state.getProperty (te::IDs::gain, 0.0);
    gainSlider.setValue (db, juce::dontSendNotification);
}

//==============================================================================
// Toolbar / tools
//==============================================================================
void AudioClipEditor::setTool (Tool t)
{
    activeTool = t;
    if (t != Tool::range && rangeActive)
    {
        rangeActive = false;
        updateToolbarState();
        repaint();
        return;
    }
    updateToolbarState();
}

void AudioClipEditor::updateToolbarState()
{
    auto setActive = [] (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId,
                     on ? kAccent.withAlpha (0.55f) : juce::Colour (0xff2a2a32));
    };

    setActive (toolPointerBtn, activeTool == Tool::pointer);
    setActive (toolSplitBtn,   activeTool == Tool::split);
    setActive (toolRangeBtn,   activeTool == Tool::range);
    setActive (snapBtn,        snapEnabled);
    setActive (muteBtn,        isClipMuted());
    setActive (autoBtn,        automationVisible);
    rangeCutBtn.setEnabled (rangeActive);
    repaint();
}

double AudioClipEditor::snapLocal (double localSecs, bool bypassSnap) const
{
    if (! snapEnabled || bypassSnap)
        return juce::jlimit (0.0, clipLenSecs(), localSecs);

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return juce::jlimit (0.0, clipLenSecs(), localSecs);

    // 1/16-Raster (Viertel-Beat) über die TempoSequence — arbeitet in ABSOLUTER
    // Edit-Zeit, damit der Raster auch bei Clips abseits des Taktanfangs stimmt.
    const double absSecs = clipStartAbsSecs() + localSecs;
    const auto   beats   = edit->tempoSequence.toBeats (TimePos::fromSeconds (absSecs));
    const double snapped = std::round (beats.inBeats() * 4.0) / 4.0;
    const double snappedAbs = edit->tempoSequence.toTime (
                                  tracktion::core::BeatPosition::fromBeats (snapped)).inSeconds();

    return juce::jlimit (0.0, clipLenSecs(), snappedAbs - clipStartAbsSecs());
}

void AudioClipEditor::splitAt (double localSecs)
{
    const double len = clipLenSecs();
    if (localSecs <= 0.001 || localSecs >= len - 0.001)
        return;   // degenerate — nothing to cut

    // cutClip trims THIS clip to the left piece and inserts the right piece on the
    // track — the editor keeps showing the (shorter) left clip, arrangement updates
    // via its own listeners. One undo step.
    clipEditor.cutClip (clip, clipStartAbsSecs() + localSecs);

    clampView();
    syncScrollBar();
    updateLaneMapping();
    repaint();
}

void AudioClipEditor::applyRangeDelete()
{
    if (! rangeActive)
        return;

    const double a = juce::jmin (rangeA, rangeB);
    const double b = juce::jmax (rangeA, rangeB);
    rangeActive = false;
    updateToolbarState();

    if (b - a < 0.005)
    {
        repaint();
        return;
    }

    // Der Atmer-Workflow (E2): Bereich markieren → Delete → Lücke bleibt, Audio
    // rechts der Lücke bleibt quellsynchron. EIN Undo-Schritt (Engine-Kern).
    // Deckt der Bereich den ganzen Clip, LÖSCHT die Engine den Clip — das
    // DockedEditorPanel hält den Clip refcounted am Leben und tauscht den Editor
    // beim nächsten Poll-Tick aus (kein UAF-Fenster).
    clipEditor.removeRangeFromClip (clip, clipStartAbsSecs() + a, clipStartAbsSecs() + b);

    clampView();
    syncScrollBar();
    updateLaneMapping();
    repaint();
}

bool AudioClipEditor::isClipMuted() const
{
    return (bool) clip.state.getProperty (te::IDs::mute, false);
}

void AudioClipEditor::toggleClipMute()
{
    // I-03: TE-Setter laufen nicht durch den UndoManager — State via
    // ValueTree-Property + UM setzen, TE zieht über seinen Listener nach.
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction (isClipMuted() ? "Clip aktivieren" : "Clip stummschalten");
    clip.state.setProperty (te::IDs::mute, ! isClipMuted(), &um);
    updateToolbarState();
}

void AudioClipEditor::toggleAutomationLane()
{
    if (automationVisible)
    {
        automationVisible = false;
        autoLane.reset();
        updateToolbarState();
        resized();
        repaint();
        return;
    }

    auto* audioTrack = dynamic_cast<te::AudioTrack*> (clip.getTrack());
    if (audioTrack == nullptr)
        return;

    // AutomationLane-Reuse (E4): die Lane mappt Zeit über zwei by-ref doubles
    // (arrangement-Konvention: absolute Sekunden + Pixel/Sekunde). Wir spiegeln
    // unsere Clip-lokale View in dieses Mapping — updateLaneMapping() hält es
    // bei Zoom/Scroll/Resize synchron.
    updateLaneMapping();
    autoLane = std::make_unique<AutomationLane> (*audioTrack, "volume", automationEngine,
                                                 lanePixelsPerSecond, laneScrollSeconds);
    // Viewport-Konvention: die Lane ist hier viewport-groß, x=0 = laneScrollSeconds.
    // Ohne das Flag landen Punkte um (Clip-Start + View-Scroll) zu FRÜH im Song
    // (der Automation-ist-unhörbar-Bug) und die Header-Controls wandern aus dem Bild.
    autoLane->setViewportTimeMapping (true);
    addAndMakeVisible (*autoLane);
    automationVisible = true;
    updateToolbarState();
    resized();
    repaint();
}

void AudioClipEditor::updateLaneMapping()
{
    const auto wa = waveArea();
    lanePixelsPerSecond = (viewLenSecs > 0.0 && wa.getWidth() > 0)
                              ? wa.getWidth() / viewLenSecs : 100.0;
    laneScrollSeconds   = clipStartAbsSecs() + viewStartSecs;

    if (autoLane != nullptr)
    {
        autoLane->updateScrollOffset();
        autoLane->repaint();
    }
}

//==============================================================================
// S1-Anker: Handover von Zeitachse + Werkzeug-Zustand über Selektionswechsel
//==============================================================================
AudioClipEditor::ViewHandover AudioClipEditor::getViewHandover() const
{
    return { clipStartAbsSecs() + viewStartSecs, viewLenSecs,
             (int) activeTool, snapEnabled, automationVisible };
}

void AudioClipEditor::applyViewHandover (const ViewHandover& h)
{
    if (h.len > 0.0)
    {
        // Absolute Fensterlage in die Lokalzeit DIESES Clips umrechnen —
        // schneidet das Fenster den Clip nicht, macht clampView() einen Fit.
        const auto w  = editorview::rebase ({ 0.0, h.len }, h.absStart, clipStartAbsSecs());
        viewStartSecs = w.startLocal;
        viewLenSecs   = w.len;
        clampView();
    }

    snapEnabled = h.snap;
    if (h.tool >= 0 && h.tool <= (int) Tool::range)
        activeTool = (Tool) h.tool;
    if (h.automationLaneOpen && ! automationVisible && autoBtn.isEnabled())
        toggleAutomationLane();

    updateToolbarState();
    syncScrollBar();
    updateLaneMapping();
    repaint();
}

//==============================================================================
// Nachbar-Clips derselben Spur — gedimmt im Zeitkontext (S1-Edit-View-Look)
//==============================================================================
juce::AudioThumbnail* AudioClipEditor::neighbourThumbFor (const juce::File& sourceFile)
{
    if (! sourceFile.existsAsFile())
        return nullptr;

    auto& slot = neighbourThumbs[sourceFile.getFullPathName()];
    if (slot == nullptr)
    {
        // App-weiter Pool-Cache (I: NIE ein Cache pro Component) — der Thumbnail
        // ist nur der Zeichen-Proxy, die gelesenen Daten teilt der Pool.
        slot = std::make_unique<juce::AudioThumbnail> (512, thumbnailPool->formatManager,
                                                       thumbnailPool->cache);
        slot->setSource (new juce::FileInputSource (sourceFile));
    }
    return slot.get();
}

void AudioClipEditor::paintNeighbourClips (juce::Graphics& g, const juce::Rectangle<int>& wa)
{
    auto* track = dynamic_cast<te::ClipTrack*> (clip.getTrack());
    if (track == nullptr)
        return;

    const double viewEnd = viewStartSecs + viewLenSecs;

    for (auto* c : track->getClips())
    {
        if (c == &clip)
            continue;
        auto* wave = dynamic_cast<te::WaveAudioClip*> (c);
        if (wave == nullptr)
            continue;

        // Nachbar in Lokalzeit DIESES Clips
        const double nStart = wave->getPosition().getStart().inSeconds() - clipStartAbsSecs();
        const double nEnd   = wave->getPosition().getEnd().inSeconds()   - clipStartAbsSecs();
        const double a = juce::jmax (nStart, viewStartSecs);
        const double b = juce::jmin (nEnd,   viewEnd);
        if (b <= a)
            continue;

        const int x0 = (int) std::floor (localSecsToX (a));
        const int x1 = (int) std::ceil  (localSecsToX (b));
        const juce::Rectangle<int> r (x0, wa.getY(), juce::jmax (1, x1 - x0), wa.getHeight());

        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (r);

        if (auto* thumb = neighbourThumbFor (wave->getSourceFileReference().getFile()))
            if (thumb->getTotalLength() > 0.0)
            {
                const double nOffset = wave->getPosition().getOffset().inSeconds();
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                thumb->drawChannels (g, r,
                                     nOffset + (a - nStart),
                                     nOffset + (b - nStart),
                                     1.0f);
            }
    }
}

//==============================================================================
void AudioClipEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (kHeaderH).reduced (6, 3);
    zoomFitButton.setBounds (header.removeFromRight (36));
    zoomInButton .setBounds (header.removeFromRight (24));
    zoomOutButton.setBounds (header.removeFromRight (24));
    header.removeFromRight (10);
    gainSlider.setBounds  (header.removeFromRight (200));
    gainCaption.setBounds (header.removeFromRight (40));
    nameLabel.setBounds   (header);

    auto toolbar = area.removeFromTop (kToolbarH).reduced (6, 2);
    toolPointerBtn.setBounds (toolbar.removeFromLeft (46));
    toolbar.removeFromLeft (2);
    toolSplitBtn.setBounds (toolbar.removeFromLeft (46));
    toolbar.removeFromLeft (2);
    toolRangeBtn.setBounds (toolbar.removeFromLeft (52));
    toolbar.removeFromLeft (10);
    snapBtn.setBounds (toolbar.removeFromLeft (46));
    toolbar.removeFromLeft (10);
    muteBtn.setBounds (toolbar.removeFromLeft (46));
    toolbar.removeFromLeft (2);
    autoBtn.setBounds (toolbar.removeFromLeft (46));
    rangeCutBtn.setBounds (toolbar.removeFromRight (86));

    hScroll.setBounds (area.removeFromBottom (kScrollH));

    if (automationVisible && autoLane != nullptr)
        autoLane->setBounds (area.removeFromBottom (kAutoLaneH));

    if (viewLenSecs <= 0.0)
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
    }
    syncScrollBar();
    updateLaneMapping();
}

void AudioClipEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c22));

    const auto wa = waveArea();

    // Header + toolbar separators
    g.setColour (juce::Colour (0xff000000));
    g.drawHorizontalLine (kHeaderH - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (kHeaderH + kToolbarH - 1, 0.0f, (float) getWidth());

    if (viewLenSecs <= 0.0)
    {
        viewStartSecs = 0.0;
        viewLenSecs   = clipLenSecs();
    }

    // ---- Nachbar-Clips derselben Spur (gedimmt, S1: Spur-Kontext bleibt sichtbar) ----
    paintNeighbourClips (g, wa);

    // ---- Waveform — NUR über der Clip-Region. Das Fenster darf über die
    // Clip-Grenzen hinausragen (S1-Anker nach Split/Range-Delete); außerhalb
    // bleibt der dunkle Spur-Hintergrund (Lücke) bzw. die Nachbar-Clips. ----
    const double len  = clipLenSecs();
    const double visA = juce::jmax (0.0, viewStartSecs);
    const double visB = juce::jmin (len, viewStartSecs + viewLenSecs);

    const bool muted = isClipMuted();
    const double offset = clip.getPosition().getOffset().inSeconds();

    if (visB > visA)
    {
        const int x0 = (int) std::floor (localSecsToX (visA));
        const int x1 = (int) std::ceil  (localSecsToX (visB));
        const juce::Rectangle<int> clipRect (x0, wa.getY(),
                                             juce::jmax (1, x1 - x0), wa.getHeight());

        g.setColour (clip.getTrack() != nullptr
                         ? clip.getTrack()->getColour().withAlpha (0.14f)
                         : juce::Colour (0xff26262e));
        g.fillRect (clipRect);

        if (thumbnail.getTotalLength() > 0.0)
        {
            const float gainDb  = (float) (double) clip.state.getProperty (te::IDs::gain, 0.0);
            const float gainLin = juce::jlimit (0.0f, 4.0f, juce::Decibels::decibelsToGain (gainDb));

            g.setColour (juce::Colours::white.withAlpha (muted ? 0.25f : 0.85f));
            thumbnail.drawChannels (g, clipRect,
                                    offset + visA,
                                    offset + visB,
                                    gainLin);
        }
        else
        {
            g.setColour (juce::Colour (0xff777777));
            g.drawText (juce::String::fromUTF8 ("Keine Audiodatei gefunden — Waveform nicht verf\xc3\xbcgbar"),
                        clipRect, juce::Justification::centred, false);
        }
    }

    if (muted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("STUMM", wa.reduced (8), juce::Justification::topRight, false);
    }

    // Centre line
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawHorizontalLine (wa.getCentreY(), (float) wa.getX(), (float) wa.getRight());

    // ---- Fade overlays (same property semantics as ClipComponent) ----
    const double fadeIn  = (double) clip.state.getProperty (te::IDs::fadeIn,  0.0);
    const double fadeOut = (double) clip.state.getProperty (te::IDs::fadeOut, 0.0);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    if (fadeIn > 0.0)
    {
        juce::Path p;
        const float x0 = localSecsToX (0.0), x1 = localSecsToX (fadeIn);
        p.startNewSubPath (x0, (float) wa.getY());
        p.lineTo (x1, (float) wa.getY());
        p.lineTo (x0, (float) wa.getBottom());
        p.closeSubPath();
        g.fillPath (p);
    }
    if (fadeOut > 0.0)
    {
        juce::Path p;
        const float x0 = localSecsToX (len - fadeOut), x1 = localSecsToX (len);
        p.startNewSubPath (x1, (float) wa.getY());
        p.lineTo (x0, (float) wa.getY());
        p.lineTo (x1, (float) wa.getBottom());
        p.closeSubPath();
        g.fillPath (p);
    }

    // Fade handles (top corners at the fade boundary)
    g.setColour (kAccent);
    g.fillEllipse (localSecsToX (fadeIn) - 5.0f,          (float) wa.getY() + 3.0f, 10.0f, 10.0f);
    g.fillEllipse (localSecsToX (len - fadeOut) - 5.0f,   (float) wa.getY() + 3.0f, 10.0f, 10.0f);

    // ---- Trim edges: an den ECHTEN Clip-Grenzen (die durch den S1-Anker
    // jetzt mitten im Fenster liegen können), nicht am Fensterrand ----
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    const float xEdgeL = localSecsToX (0.0);
    const float xEdgeR = localSecsToX (len);
    if (xEdgeL >= wa.getX() && xEdgeL <= wa.getRight())
        g.fillRect ((int) xEdgeL, wa.getY(), 3, wa.getHeight());
    if (xEdgeR >= wa.getX() && xEdgeR <= wa.getRight())
        g.fillRect ((int) xEdgeR - 3, wa.getY(), 3, wa.getHeight());

    // ---- Playhead ----
    if (auto* edit = editManager.getEdit())
    {
        const double tpos  = edit->getTransport().getPosition().inSeconds();
        const double local = tpos - clipStartAbsSecs();
        if (local >= viewStartSecs && local <= viewStartSecs + viewLenSecs)
        {
            g.setColour (juce::Colours::white);
            const float x = localSecsToX (local);
            g.drawLine (x, (float) wa.getY(), x, (float) wa.getBottom(), 1.5f);
        }
    }
}

void AudioClipEditor::paintOverChildren (juce::Graphics& g)
{
    // I-05: Overlays gehören in paintOverChildren, sonst verschwinden sie unter
    // Kind-Komponenten (Scrollbar, Automation-Lane).
    if (! rangeActive)
        return;

    const auto wa = waveArea();
    const float x0 = localSecsToX (juce::jmin (rangeA, rangeB));
    const float x1 = localSecsToX (juce::jmax (rangeA, rangeB));

    juce::Rectangle<float> r (x0, (float) wa.getY(),
                              juce::jmax (1.0f, x1 - x0), (float) wa.getHeight());
    g.setColour (kAccent.withAlpha (0.20f));
    g.fillRect (r);
    g.setColour (kAccent);
    g.drawRect (r, 1.0f);
}

//==============================================================================
void AudioClipEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto wa = waveArea();
    if (! wa.contains (e.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    if (activeTool == Tool::split)
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        return;
    }
    if (activeTool == Tool::range)
    {
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        return;
    }

    // Trim-Zonen liegen an den ECHTEN Clip-Kanten (können mitten im Fenster sein)
    const float xEdgeL = localSecsToX (0.0);
    const float xEdgeR = localSecsToX (clipLenSecs());
    const bool nearLeft  = std::abs ((float) e.x - xEdgeL) <= (float) kEdgeZone;
    const bool nearRight = std::abs ((float) e.x - xEdgeR) <= (float) kEdgeZone;
    setMouseCursor ((nearLeft || nearRight) ? juce::MouseCursor::LeftRightResizeCursor
                                            : juce::MouseCursor::IBeamCursor);
}

void AudioClipEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();   // Tastatur-Werkzeuge (1/2/3, C, N, Delete) ab dem ersten Klick

    const auto wa = waveArea();
    if (! wa.contains (e.getPosition()))
        return;

    // ---- Split-Werkzeug: Klick = Schnitt (gesnappt, Alt = frei) ----
    if (activeTool == Tool::split)
    {
        splitAt (snapLocal (xToLocalSecs ((float) e.x), e.mods.isAltDown()));
        return;
    }

    // ---- Range: eigenes Werkzeug ODER Cmd+Ziehen mit dem Pfeil (Arrange-Konvention) ----
    if (activeTool == Tool::range
        || (activeTool == Tool::pointer && e.mods.isCommandDown()))
    {
        rangeDragging = true;
        rangeActive   = true;
        rangeA = rangeB = snapLocal (xToLocalSecs ((float) e.x), e.mods.isAltDown());
        updateToolbarState();
        repaint();
        return;
    }

    // Pfeil-Klick räumt eine stehende Range weg
    if (rangeActive)
    {
        rangeActive = false;
        updateToolbarState();
        repaint();
    }

    const double fadeIn  = (double) clip.state.getProperty (te::IDs::fadeIn,  0.0);
    const double fadeOut = (double) clip.state.getProperty (te::IDs::fadeOut, 0.0);
    const double len     = clipLenSecs();

    dragStartScreenX    = e.getScreenX();
    dragStartClipStart  = clip.getPosition().getStart().inSeconds();
    dragStartClipEnd    = clip.getPosition().getEnd().inSeconds();
    dragStartOffset     = clip.getPosition().getOffset().inSeconds();

    // Fade handles first (they sit at the top edge)
    if (e.y < wa.getY() + kFadeGrab + 4)
    {
        if (std::abs (e.x - localSecsToX (fadeIn)) <= kFadeGrab)
        {
            activeDrag = Drag::fadeIn;
            dragStartFadeSecs = fadeIn;
            editManager.getUndoManager().beginNewTransaction ("Fade in");
            return;
        }
        if (std::abs (e.x - localSecsToX (len - fadeOut)) <= kFadeGrab)
        {
            activeDrag = Drag::fadeOut;
            dragStartFadeSecs = fadeOut;
            editManager.getUndoManager().beginNewTransaction ("Fade out");
            return;
        }
    }

    // Trim zones — an den echten Clip-Kanten (S1-Anker: Kante kann im Fenster liegen)
    dragStartViewAbs = dragStartClipStart + viewStartSecs;
    if (std::abs ((float) e.x - localSecsToX (0.0)) <= (float) kEdgeZone)
    {
        activeDrag = Drag::trimLeft;
        trimMaxExtendLeft = dragStartClipStart - dragStartOffset;   // can't reveal before source start
        editManager.getUndoManager().beginNewTransaction ("Trim clip");
        return;
    }
    if (std::abs ((float) e.x - localSecsToX (len)) <= (float) kEdgeZone)
    {
        activeDrag = Drag::trimRight;
        trimMaxExtendRight = dragStartClipStart + (sourceLenSecs() - dragStartOffset);
        editManager.getUndoManager().beginNewTransaction ("Trim clip");
        return;
    }

    // Plain click = seek transport to that position (auch außerhalb des Clips —
    // die Zeitachse ist absolut, S1-Verhalten)
    if (auto* edit = editManager.getEdit())
    {
        const double absSecs = juce::jmax (0.0, dragStartClipStart + xToLocalSecs ((float) e.x));
        edit->getTransport().setPosition (TimePos::fromSeconds (absSecs));
        repaint();
    }
}

void AudioClipEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // S1: Doppelklick schneidet — unabhängig vom aktiven Werkzeug (außer Range,
    // dort wäre es ein Konflikt mit dem Aufziehen).
    const auto wa = waveArea();
    if (activeTool != Tool::range && wa.contains (e.getPosition()))
        splitAt (snapLocal (xToLocalSecs ((float) e.x), e.mods.isAltDown()));
}

void AudioClipEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (rangeDragging)
    {
        rangeB = snapLocal (xToLocalSecs ((float) e.x), e.mods.isAltDown());
        repaint();
        return;
    }

    if (activeDrag == Drag::none)
        return;

    const auto wa = waveArea();
    const double pxPerSec = viewLenSecs > 0.0 ? wa.getWidth() / viewLenSecs : 1.0;
    const double deltaSecs = (e.getScreenX() - dragStartScreenX) / juce::jmax (1.0, pxPerSec);
    auto& um = editManager.getUndoManager();

    if (activeDrag == Drag::fadeIn)
    {
        const double len = clipLenSecs();
        const double f = juce::jlimit (0.0, len, dragStartFadeSecs + deltaSecs);
        clip.state.setProperty (te::IDs::fadeIn, f, &um);
        repaint();
        return;
    }
    if (activeDrag == Drag::fadeOut)
    {
        const double len = clipLenSecs();
        const double f = juce::jlimit (0.0, len, dragStartFadeSecs - deltaSecs);
        clip.state.setProperty (te::IDs::fadeOut, f, &um);
        repaint();
        return;
    }
    if (activeDrag == Drag::trimLeft)
    {
        // Left trim keeps source sync: offset moves with the start (ClipComponent semantics)
        const double newStart = juce::jlimit (trimMaxExtendLeft,
                                              dragStartClipEnd - 0.05,
                                              dragStartClipStart + deltaSecs);
        const double newOffset = dragStartOffset + (newStart - dragStartClipStart);
        clip.setPosition ({ TimeRng (TimePos::fromSeconds (newStart),
                                     TimePos::fromSeconds (dragStartClipEnd)),
                            TimeDur::fromSeconds (newOffset) });
        // S1-Anker: der Clip-URSPRUNG wandert beim Left-Trim — Lokalzeit so
        // nachführen, dass die absolute Fensterlage (und damit das Bild) steht.
        viewStartSecs = dragStartViewAbs - newStart;
        clampView();
        syncScrollBar();
        updateLaneMapping();
        repaint();
        return;
    }
    if (activeDrag == Drag::trimRight)
    {
        const double newEnd = juce::jlimit (dragStartClipStart + 0.05,
                                            trimMaxExtendRight,
                                            dragStartClipEnd + deltaSecs);
        clip.setPosition ({ TimeRng (TimePos::fromSeconds (dragStartClipStart),
                                     TimePos::fromSeconds (newEnd)),
                            TimeDur::fromSeconds (dragStartOffset) });
        // Ursprung unverändert — Fenster steht von selbst (kein Fit-Reset mehr)
        clampView();
        syncScrollBar();
        updateLaneMapping();
        repaint();
        return;
    }
}

void AudioClipEditor::mouseUp (const juce::MouseEvent&)
{
    if (rangeDragging)
    {
        rangeDragging = false;
        if (std::abs (rangeB - rangeA) < 0.005)
            rangeActive = false;   // reiner Klick — keine Range stehen lassen
        updateToolbarState();
        repaint();
    }
    activeDrag = Drag::none;
}

bool AudioClipEditor::keyPressed (const juce::KeyPress& key)
{
    const int kc = key.getKeyCode();

    if (kc == '1') { setTool (Tool::pointer); return true; }
    if (kc == '2' || kc == 'c' || kc == 'C')
    {
        setTool (activeTool == Tool::split ? Tool::pointer : Tool::split);
        return true;
    }
    if (kc == '3') { setTool (Tool::range); return true; }

    if (kc == 'n' || kc == 'N')
    {
        snapEnabled = ! snapEnabled;
        updateToolbarState();
        return true;
    }

    if (kc == juce::KeyPress::escapeKey)
    {
        if (rangeActive)             { rangeActive = false; updateToolbarState(); repaint(); return true; }
        if (activeTool != Tool::pointer) { setTool (Tool::pointer); return true; }
        return false;   // nichts zu räumen → MainComponent darf den Dock schließen
    }

    // Delete/Backspace mit aktiver Range: Bereich aus dem Clip entfernen.
    // Konsumiert VOR dem globalen Delete-Forward in MainComponent — sonst würde
    // stattdessen die Arrangement-Selektion gelöscht.
    if ((kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey) && rangeActive)
    {
        applyRangeDelete();
        return true;
    }

    return false;
}

void AudioClipEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        // Cmd+scroll = zoom around cursor (same convention as the arrangement)
        zoomAround (wheel.deltaY > 0 ? 1.0 / 1.15 : 1.15, (float) e.x);
        return;
    }

    // Plain scroll = horizontal pan
    const double step = (wheel.deltaX != 0.0f ? -wheel.deltaX : -wheel.deltaY) * viewLenSecs * 0.25;
    viewStartSecs += step;
    clampView();
    syncScrollBar();
    updateLaneMapping();
    repaint();
}

void AudioClipEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (scaleFactor > 0.0f)
        zoomAround (1.0 / (double) scaleFactor, (float) e.x);
}

//==============================================================================
void AudioClipEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop)
{
    // May fire off the message thread (undo etc.) — defer UI work.
    juce::Component::SafePointer<AudioClipEditor> safe (this);
    juce::MessageManager::callAsync ([safe, prop]
    {
        if (safe == nullptr)
            return;
        if (prop == te::IDs::gain)
            safe->syncGainSlider();
        if (prop == te::IDs::mute)
            safe->updateToolbarState();
        safe->clampView();
        safe->syncScrollBar();
        safe->updateLaneMapping();
        safe->repaint();
    });
}

void AudioClipEditor::timerCallback()
{
    // Cheap playhead-only repaint while playing
    if (auto* edit = editManager.getEdit())
        if (edit->getTransport().isPlaying())
            repaint (waveArea());
}

void AudioClipEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewStartSecs = newRangeStart;
    clampView();
    updateLaneMapping();
    repaint();
}
