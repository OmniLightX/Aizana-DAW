#include "ClipComponent.h"
#include "MidiClipPreview.h"
#include "ArrangementView.h"
#include "app/MainComponent.h"
#include "engine/ClipEditor.h"
#include "engine/AizanaEngineBehaviour.h"
#include "engine/ClipGainEnvelope.h"
#include "ui/common/TrackColours.h"
#include "ui/pluginbrowser/PluginBrowserPopup.h"
#include "engine/TempoDetector.h"
#include "engine/TempoEngine.h"
#include "engine/VocalTuner.h"

// Strong time-type aliases — same as ClipEditor.cpp, needed for clip.setPosition()
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using TimeDur = tracktion::core::TimeDuration;

//==============================================================================

ClipComponent::ClipComponent (te::Clip& clip_, EditManager& editManager_,
                               ClipEditor& clipEditor_, double& pixelsPerSecond_,
                               const EditTool& activeTool)
    : clip (clip_), clipRef (&clip_), editManager (editManager_), clipEditor (clipEditor_),
      pixelsPerSecond (pixelsPerSecond_), activeTool_ (activeTool)
{
    setRepaintsOnMouseActivity (true);

    // Benennung wie in der a11y-Welle (f846eb9): ohne Namen ist ein Clip nur ueber
    // Bildschirmkoordinaten erreichbar — die bei jedem Scrollen, Zoomen oder
    // Verschieben still ins Leere greifen und trotzdem Erfolg melden. Gezeichnet
    // wird der Titel nicht (der sichtbare Clip-Name kommt aus paint()).
    setTitle (juce::String::fromUTF8 ("Clip ") + clip.getName());

    // Tell JUCE 8's Viewport DragToScrollListener not to hijack drag events
    // over clips — the clip handles its own dragging.
    setViewportIgnoreDragFlag (true);
    // Prevent click on a clip from stealing keyboard focus away from ArrangementView.
    // ClipComponent handles only mouse events — all keyboard shortcuts live in AV.
    setMouseClickGrabsKeyboardFocus (false);

    // Audio formats are registered once in the shared AudioThumbnailPool.

    clip.addSelectableListener (this);
    clip.state.addListener (this);
    refreshThumbnail();
}

ClipComponent::~ClipComponent()
{
    clip.removeSelectableListener (this);
    clip.state.removeListener (this);
    if (thumbnail != nullptr)
        thumbnail->removeChangeListener (this);
}

void ClipComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (dynamic_cast<te::MidiClip*> (&clip) != nullptr)
    {
        repaint();
        return;
    }
    // Mute changes (mute tool, dock editor, undo) redraw the dimmed look immediately.
    if (tree == clip.state && prop == te::IDs::mute)
    {
        repaint();
        return;
    }

    if (tree != clip.state) return;

    // ValueTree source writes may precede TE's effective source update;
    // SelectableListener also refreshes after that asynchronous update.
    refreshThumbnail();
}

void ClipComponent::refreshThumbnail()
{
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (waveClip == nullptr) return;
    const auto audioFile = waveClip->getCurrentSourceFile();
    if (audioFile == thumbnailFile) return;
    if (thumbnail != nullptr) thumbnail->removeChangeListener (this);
    thumbnail = nullptr;
    thumbnailLoaded = false;
    thumbnailFile = juce::File();
    if (audioFile.existsAsFile())
    {
        thumbnail = &thumbnailPool->forFile (audioFile);
        thumbnail->addChangeListener (this);
        thumbnailLoaded = true;
        thumbnailFile = audioFile;
    }
    repaint();
}

void ClipComponent::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    repaint();
}

void ClipComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    constexpr float kCornerRadius = 4.0f;
    constexpr float kHeaderHeight = 16.0f;  // clip name header band height

    // ---- Colour calculation ----
    // Use the shared track colour (user-set, or a stable palette default for
    // uncoloured tracks — see AizanaTrackColours). Boost saturation + brightness
    // so clips stay vivid and legible on the dark background.
    juce::Colour baseColour = juce::Colour (0xff2a6aaa);  // Studio One default blue (fallback)
    if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
    {
        const juce::Colour trackColour = AizanaTrackColours::displayColour (*track);
        baseColour = trackColour.withSaturation (juce::jmax (0.55f, trackColour.getSaturation()))
                                .withBrightness  (juce::jmax (0.65f, trackColour.getBrightness()));
    }

    // MIDI clips: shift hue slightly so they are visually distinct from audio clips
    if (dynamic_cast<te::MidiClip*> (&clip) != nullptr)
        baseColour = baseColour.withHue (baseColour.getHue() + 0.08f)
                               .withSaturation (juce::jmin (1.0f, baseColour.getSaturation() + 0.05f));

    // Muted clips (mute tool / dock editor) draw grey and dimmed — the Studio One
    // "event inactive" look. All derived colours below inherit the desaturation.
    const bool clipMuted = (bool) clip.state.getProperty (te::IDs::mute, false);
    if (clipMuted)
        baseColour = baseColour.withSaturation (0.12f).withBrightness (0.42f);

    const juce::Colour headerColour = baseColour.darker (0.18f);

    // ---- Clip body (below header) ----
    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds.reduced (1.0f), kCornerRadius);

    // ---- Header band (top strip) ----
    // Clip the header to the top rounded portion so the corners are correct.
    {
        juce::Path headerPath;
        const auto headerRect = bounds.reduced (1.0f)
                                      .withHeight (kHeaderHeight + kCornerRadius);
        headerPath.addRoundedRectangle (headerRect.getX(), headerRect.getY(),
                                        headerRect.getWidth(), headerRect.getHeight(),
                                        kCornerRadius, kCornerRadius,
                                        true, true,   // top-left, top-right rounded
                                        false, false); // bottom-left, bottom-right square
        g.setColour (headerColour);
        g.fillPath (headerPath);
    }

    // ---- Separator line between header and body ----
    g.setColour (baseColour.darker (0.35f));
    g.drawHorizontalLine (juce::roundToInt (bounds.getY() + 1.0f + kHeaderHeight),
                          bounds.getX() + 1.0f,
                          bounds.getRight() - 1.0f);

    // ---- Clip name in header ----
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (11.0f));

    // Takes badge: "T2/3" shown right-aligned in header when clip has multiple takes;
    // shows "C" while a Aizana comp is active (audio is assembled from several takes).
    int numTakes = 0;
    int currentTakeIdx = 0;
    bool compActive = false;
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        numTakes       = waveClip->getNumTakes (false);
        currentTakeIdx = juce::jmax (0, clipEditor.getActiveTake (*waveClip));
        compActive     = CompingEngine::isCompActive (*waveClip);
    }
    const float badgeWidth = (numTakes > 1 && getWidth() >= 50) ? 30.0f : 0.0f;

    // "fx" badge when the clip carries Event-FX (its own live plugin chain)
    bool hasEventFx = false;
    if (auto* clipPlugins = clip.getPluginList())
        for (auto* p : clipPlugins->getPlugins())
        {
            const juce::String t = p->getPluginType();
            if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
            {
                hasEventFx = true;
                break;
            }
        }
    const float fxBadgeWidth = (hasEventFx && getWidth() >= 70) ? 20.0f : 0.0f;

    const auto nameRect = juce::Rectangle<float> (bounds.getX() + 5.0f,
                                                   bounds.getY() + 1.0f,
                                                   bounds.getWidth() - 8.0f - badgeWidth - fxBadgeWidth,
                                                   kHeaderHeight).toNearestInt();
    g.drawText (clip.getName(), nameRect, juce::Justification::centredLeft, true);

    if (fxBadgeWidth > 0.0f)
    {
        const auto fxRect = juce::Rectangle<float> (bounds.getRight() - 1.0f - badgeWidth - fxBadgeWidth,
                                                     bounds.getY() + 2.0f,
                                                     fxBadgeWidth - 2.0f,
                                                     kHeaderHeight - 4.0f);
        g.setColour (juce::Colour (0xff44cc66).withAlpha (0.35f));
        g.fillRoundedRectangle (fxRect, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("fx", fxRect.toNearestInt(), juce::Justification::centred, false);
        g.setFont (juce::FontOptions (11.0f));
    }

    if (badgeWidth > 0.0f)
    {
        const auto badgeRect = takesBadgeBounds();
        takesBadgeHit = badgeRect.toNearestInt();
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (badgeRect, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (9.0f));
        const juce::String badgeText = compActive
            ? "C/" + juce::String (numTakes)
            : "T" + juce::String (currentTakeIdx + 1) + "/" + juce::String (numTakes);
        g.drawText (badgeText, badgeRect.toNearestInt(), juce::Justification::centred, false);
        g.setFont (juce::FontOptions (11.0f)); // restore font for subsequent draw calls
    }

    if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
    {
        const auto notes = aizana::midiClipPreview (*midi);
        if (! notes.empty())
        {
            const auto area = bounds.withTrimmedTop (kHeaderHeight + 3.0f).reduced (2.0f);
            if (! area.isEmpty())
            {
                int low = 127, high = 0;
                for (const auto& n : notes) { low = juce::jmin (low, n.pitch); high = juce::jmax (high, n.pitch); }
                const float row = area.getHeight() / juce::jmax (12, high - low + 3);
                const float centre = (high + low) * 0.5f;
                const double duration = juce::jmax (0.001, clip.getPosition().getLength().inSeconds());
                juce::Graphics::ScopedSaveState saved (g);
                g.reduceClipRegion (area.toNearestInt());
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                for (const auto& n : notes)
                {
                    const float x = area.getX() + float (n.start / duration) * area.getWidth();
                    const float y = area.getCentreY() + (centre - n.pitch) * row;
                    g.fillRect (x, y, juce::jmax (2.0f, float ((n.end - n.start) / duration) * area.getWidth()),
                                juce::jmax (1.0f, juce::jmin (4.0f, row * 0.8f)));
                }
            }
        }
    }

    // ---- Waveform thumbnail (WaveAudioClip only) ----
    if (thumbnailLoaded && thumbnail != nullptr && thumbnail->getTotalLength() > 0.0)
    {
        // Sync currentGainDb from TE state when not dragging
        if (!gainDragActive)
        {
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
                currentGainDb = wave->getGainDB();
            else
                currentGainDb = 0.0f;
        }

        // Draw waveform in the body area below the header band
        const float waveTop    = bounds.getY() + 1.0f + kHeaderHeight + 2.0f;
        const float waveBottom = bounds.getBottom() - 2.0f;
        if (waveBottom - waveTop > 4.0f)
        {
            auto waveArea = juce::Rectangle<int> (
                juce::roundToInt (bounds.getX() + 2.0f),
                juce::roundToInt (waveTop),
                juce::roundToInt (bounds.getWidth() - 4.0f),
                juce::roundToInt (waveBottom - waveTop));

            // White waveform — amplitude scaled by clip gain (clamped to 4× max visual scale)
            g.setColour (juce::Colours::white.withAlpha (0.50f));

            const float gainLinear  = juce::Decibels::decibelsToGain (currentGainDb);
            const float clampedGain = juce::jlimit (0.0f, 4.0f, gainLinear);

            // Source-offset-aware: split clips show the correct waveform portion
            const double clipOffset     = clip.getPosition().getOffset().inSeconds();
            const double clipLengthSecs = clip.getPosition().getLength().inSeconds();
            // NUR DEN SICHTBAREN AUSSCHNITT zeichnen, nicht den ganzen Clip.
            //
            // Ein Clip ist so breit wie seine Dauer mal Zoom — bei einer 165-s-Datei
            // sind das 16458 px, waehrend das Fenster ~670 zeigt. JUCEs AudioThumbnail
            // rechnet dann eine Spalte je Pixel ueber die volle Breite; das kostete
            // nicht nur unnoetig Zeit, sondern lieferte eine flache Linie
            // (GUI-Testlauf 2026-08-12: Quelle aufgeloest, Thumbnail geladen, Laenge
            // 164.628 s, Verstaerkung 0 dB — und trotzdem keine Ausschlaege, waehrend
            // der Dock-Editor dieselbe Datei in ~1400 px sauber zeichnete).
            //
            // Der sichtbare Ausschnitt kommt aus dem Clip-Bereich des Graphics-Kontexts.
            // Die Zeitgrenzen werden exakt mitgerechnet, damit die Wellenform an
            // derselben Stelle steht wie zuvor.
            const auto sichtbar = waveArea.getIntersection (g.getClipBounds());

            if (! sichtbar.isEmpty())
            {
                const double sekProPixel = clipLengthSecs / (double) juce::jmax (1, waveArea.getWidth());
                const double vonSek = clipOffset + (sichtbar.getX() - waveArea.getX()) * sekProPixel;
                const double bisSek = vonSek + sichtbar.getWidth() * sekProPixel;

                thumbnail->drawChannel (g,
                                        sichtbar,
                                        vonSek,
                                        juce::jmax (vonSek + 0.001, bisSek),
                                        0,    // channel 0 (left/mono)
                                        1.0f * clampedGain);
            }
        }
    }

    // ---- Fade overlays and handles (WaveAudioClip only) ----
    if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr)
    {
        const float fadeInSecs  = (float)(double)clip.state.getProperty (te::IDs::fadeIn,  0.0);
        const float fadeOutSecs = (float)(double)clip.state.getProperty (te::IDs::fadeOut, 0.0);

        // Sync continuous curve values from TE integer type when the curve dot is NOT being dragged.
        // During a curve drag, fadeInCurve/fadeOutCurve are updated live by mouseDrag for smooth rendering.
        if (activeFadeDrag != FadeDragType::FadeInCurve)
        {
            const int t = (int)(double)clip.state.getProperty (te::IDs::fadeInType,  0);
            fadeInCurve  = (t == 1) ? 1.0f : (t == 2) ? -1.0f : 0.0f;
        }
        if (activeFadeDrag != FadeDragType::FadeOutCurve)
        {
            const int t = (int)(double)clip.state.getProperty (te::IDs::fadeOutType, 0);
            fadeOutCurve = (t == 1) ? 1.0f : (t == 2) ? -1.0f : 0.0f;
        }

        drawFadeOverlay (g, fadeInSecs, fadeOutSecs);
    }

    // ---- P5: Gain-Hüllkurve (read-only Anzeige; editiert wird im Dock-Editor) ----
    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        if (ClipGainEnvelope::hasEnvelope (*wc))
        {
            // dB→y: +6 oben, -30 unten (gleiche Skala wie der Dock-Editor)
            const double clipStart = clip.getPosition().getStart().inSeconds();
            const double clipLen   = juce::jmax (0.001, clip.getPosition().getLength().inSeconds());
            const auto b = getLocalBounds().toFloat();

            auto dbToY = [&b] (float db)
            {
                const float norm = (6.0f - juce::jlimit (-30.0f, 6.0f, db)) / 36.0f;
                return b.getY() + norm * b.getHeight();
            };
            auto timeToX = [&] (double editSecs)
            {
                return (float) (b.getX() + (editSecs - clipStart) / clipLen * b.getWidth());
            };

            const int n = ClipGainEnvelope::numPoints (*wc);
            juce::Path env;
            env.startNewSubPath (b.getX(), dbToY (ClipGainEnvelope::pointDb (*wc, 0)));
            for (int i = 0; i < n; ++i)
                env.lineTo (timeToX (ClipGainEnvelope::pointTime (*wc, i)),
                            dbToY (ClipGainEnvelope::pointDb (*wc, i)));
            env.lineTo (b.getRight(), dbToY (ClipGainEnvelope::pointDb (*wc, n - 1)));

            g.setColour (juce::Colour (0xffffc24d).withAlpha (0.65f));
            g.strokePath (env, juce::PathStrokeType (1.5f));
        }
    }

    // ---- Outer border — always visible ----
    const bool hovered = isMouseOver (false);
    if (hovered && !selected)
    {
        // Hover: bright border to signal interactivity
        g.setColour (baseColour.brighter (0.35f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), kCornerRadius, 1.5f);
    }
    else if (!selected)
    {
        // Normal: subtle darker border so the clip has a clear boundary
        g.setColour (baseColour.darker (0.45f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), kCornerRadius, 1.0f);
    }

    // ---- Selection highlight ----
    if (selected)
    {
        // Bright white overlay tint + thick white border
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds.reduced (1.0f), kCornerRadius);
        g.setColour (juce::Colours::white.withAlpha (0.90f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), kCornerRadius, 2.0f);
    }

    // ---- Ghost dim overlay ----
    // Drawn before gain handle and trim handles so they remain visible through the tint.
    // Applied when this clip is being ghost-dragged to another track — it will land
    // on the destination track on mouseUp. Alpha matches typical DAW "ghost source" style.
    if (isGhostDimmed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bounds.reduced (1.0f), kCornerRadius);
    }

    // ---- Clip Gain handle (WaveAudioClip only, min width guard) ----
    if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr && getWidth() >= 60)
    {
        const auto handleRect = getGainHandleRect();

        // White square with subtle grey border
        g.setColour (juce::Colours::white);
        g.fillRect (handleRect);
        g.setColour (juce::Colour (0xff888888));
        g.drawRect (handleRect, 1.0f);

        // Show dB label near the handle while dragging
        if (gainDragActive)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (10.0f));
            const juce::String label = (currentGainDb >= 0.0f ? "+" : "")
                                       + juce::String (currentGainDb, 1) + " dB";
            g.drawText (label,
                        (int)handleRect.getX() - 20, (int)handleRect.getBottom() + 2,
                        50, 14,
                        juce::Justification::centred, false);
        }
    }

    // ---- Trim handles — subtle vertical lines at bottom-half edges (Pointer mode, WaveAudioClip) ----
    if (activeTool_ == EditTool::Pointer
        && dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr)
    {
        const float halfH = (float)getHeight() * 0.5f;
        const auto mousePos = getMouseXYRelative().toFloat();

        // Left trim line
        const bool leftHovered = getLeftTrimZone().contains (mousePos)
                                 || activeTrimSide == TrimSide::Left;
        g.setColour (juce::Colours::white.withAlpha (leftHovered ? 0.9f : 0.35f));
        g.drawLine (1.0f, halfH, 1.0f, (float)getHeight(), 2.0f);

        // Right trim line
        const bool rightHovered = getRightTrimZone().contains (mousePos)
                                  || activeTrimSide == TrimSide::Right;
        g.setColour (juce::Colours::white.withAlpha (rightHovered ? 0.9f : 0.35f));
        g.drawLine ((float)getWidth() - 1.0f, halfH,
                    (float)getWidth() - 1.0f, (float)getHeight(), 2.0f);
    }
}

//==============================================================================
// Fade overlay rendering
//==============================================================================

void ClipComponent::drawFadeOverlay (juce::Graphics& g, float fadeInSecs, float fadeOutSecs)
{
    const auto bounds = getLocalBounds().toFloat();
    constexpr float kHeaderHeight = 16.0f;

    // Body area coordinates (below the header strip)
    const float bodyTop    = bounds.getY() + 1.0f + kHeaderHeight;
    const float bodyBottom = bounds.getBottom() - 1.0f;
    const float bodyLeft   = bounds.getX() + 1.0f;
    const float bodyRight  = bounds.getRight() - 1.0f;
    const float bodyWidth  = bodyRight - bodyLeft;

    const double clipLenSecs = clip.getPosition().getLength().inSeconds();

    // ----- Fade In overlay -----
    if (fadeInSecs > 0.0f && clipLenSecs > 0.0)
    {
        const float fadeInPx = juce::jmin (bodyWidth,
                                           (float)(fadeInSecs / (float)clipLenSecs) * bodyWidth);

        // Dark semi-transparent triangle: fills the fade-in region from top-left corner
        // of the body down to the body floor.
        // Triangle vertices: (bodyLeft, bodyTop) → (bodyLeft+fadeInPx, bodyTop) → (bodyLeft, bodyBottom)
        juce::Path fadeInPath;
        fadeInPath.startNewSubPath (bodyLeft, bodyTop);
        fadeInPath.lineTo (bodyLeft + fadeInPx, bodyTop);
        fadeInPath.lineTo (bodyLeft, bodyBottom);
        fadeInPath.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadeInPath);

        // Fade line — quadratic bezier so it bows based on fadeInCurve.
        // Control point starts at the chord midpoint (= straight line) and is shifted
        // horizontally by fadeInCurve * fadeInPx * 0.4:
        //   +1.0 → bow toward upper-left (logarithmic — fast rise)
        //    0.0 → straight line (linear)
        //   -1.0 → bow toward lower-right (exponential — slow rise)
        {
            const float ctrlX = bodyLeft + fadeInPx * 0.5f - fadeInCurve * fadeInPx * 0.4f;
            const float ctrlY = (bodyBottom + bodyTop) * 0.5f;
            juce::Path fadeLine;
            fadeLine.startNewSubPath (bodyLeft, bodyBottom);
            fadeLine.quadraticTo (ctrlX, ctrlY, bodyLeft + fadeInPx, bodyTop);
            g.setColour (juce::Colours::white.withAlpha (0.70f));
            g.strokePath (fadeLine, juce::PathStrokeType (1.5f));
        }

        // Curve control dot at midpoint of the diagonal
        const float dotX = bodyLeft + fadeInPx * 0.5f;
        const float dotY = (bodyBottom + bodyTop) * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.drawEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f, 1.0f);
    }

    // ----- Fade Out overlay -----
    if (fadeOutSecs > 0.0f && clipLenSecs > 0.0)
    {
        const float fadeOutPx = juce::jmin (bodyWidth,
                                            (float)(fadeOutSecs / (float)clipLenSecs) * bodyWidth);

        // Triangle: (bodyRight-fadeOutPx, bodyTop) → (bodyRight, bodyTop) → (bodyRight, bodyBottom)
        juce::Path fadeOutPath;
        fadeOutPath.startNewSubPath (bodyRight - fadeOutPx, bodyTop);
        fadeOutPath.lineTo (bodyRight, bodyTop);
        fadeOutPath.lineTo (bodyRight, bodyBottom);
        fadeOutPath.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadeOutPath);

        // Fade line — quadratic bezier bowing based on fadeOutCurve.
        // Control point at chord midpoint (straight) + horizontal offset:
        //   +1.0 → bow toward upper-right (logarithmic — fast decay)
        //    0.0 → straight line (linear)
        //   -1.0 → bow toward lower-left (exponential — slow decay)
        {
            const float ctrlX = bodyRight - fadeOutPx * 0.5f + fadeOutCurve * fadeOutPx * 0.4f;
            const float ctrlY = (bodyBottom + bodyTop) * 0.5f;
            juce::Path fadeLine;
            fadeLine.startNewSubPath (bodyRight - fadeOutPx, bodyTop);
            fadeLine.quadraticTo (ctrlX, ctrlY, bodyRight, bodyBottom);
            g.setColour (juce::Colours::white.withAlpha (0.70f));
            g.strokePath (fadeLine, juce::PathStrokeType (1.5f));
        }

        // Curve control dot at midpoint of the diagonal
        const float dotX = bodyRight - fadeOutPx * 0.5f;
        const float dotY = (bodyBottom + bodyTop) * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.drawEllipse (dotX - 4.0f, dotY - 4.0f, 8.0f, 8.0f, 1.0f);
    }

    // ----- Fade In handle (top-left corner, always visible) -----
    {
        constexpr float kHandle = 10.0f;
        const float alpha = selected ? 0.85f : 0.50f;
        const float hx = bodyLeft;
        const float hy = bodyTop;

        // Right-pointing triangle: (hx, hy) → (hx+kHandle, hy) → (hx, hy+kHandle)
        juce::Path inHandle;
        inHandle.startNewSubPath (hx, hy);
        inHandle.lineTo (hx + kHandle, hy);
        inHandle.lineTo (hx, hy + kHandle);
        inHandle.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillPath (inHandle);
        g.setColour (juce::Colours::black.withAlpha (alpha * 0.5f));
        g.strokePath (inHandle, juce::PathStrokeType (1.0f));
    }

    // ----- Fade Out handle (top-right corner, always visible) -----
    {
        constexpr float kHandle = 10.0f;
        const float alpha = selected ? 0.85f : 0.50f;
        const float hx = bodyRight;
        const float hy = bodyTop;

        // Left-pointing triangle: (hx-kHandle, hy) → (hx, hy) → (hx, hy+kHandle)
        juce::Path outHandle;
        outHandle.startNewSubPath (hx - kHandle, hy);
        outHandle.lineTo (hx, hy);
        outHandle.lineTo (hx, hy + kHandle);
        outHandle.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillPath (outHandle);
        g.setColour (juce::Colours::black.withAlpha (alpha * 0.5f));
        g.strokePath (outHandle, juce::PathStrokeType (1.0f));
    }
}

//==============================================================================
// Fade hit testing
//==============================================================================

ClipComponent::FadeDragType ClipComponent::getFadeHandleAt (juce::Point<int> localPos) const
{
    // Only for audio clips
    if (dynamic_cast<te::WaveAudioClip*> (&clip) == nullptr)
        return FadeDragType::None;

    constexpr float kHeaderHeight = 16.0f;
    const auto bounds = getLocalBounds().toFloat();

    const float bodyTop    = bounds.getY() + 1.0f + kHeaderHeight;
    const float bodyBottom = bounds.getBottom() - 1.0f;
    const float bodyLeft   = bounds.getX() + 1.0f;
    const float bodyRight  = bounds.getRight() - 1.0f;
    const float bodyWidth  = bodyRight - bodyLeft;
    const double clipLenSecs = clip.getPosition().getLength().inSeconds();

    const float px = (float)localPos.x;
    const float py = (float)localPos.y;

    // Check curve dots first (higher priority, tighter target)
    constexpr float kCurveTolerance = 12.0f;

    const float fadeInSecs  = (float)(double)clip.state.getProperty (te::IDs::fadeIn,  0.0);
    const float fadeOutSecs = (float)(double)clip.state.getProperty (te::IDs::fadeOut, 0.0);

    if (fadeInSecs > 0.0f && clipLenSecs > 0.0)
    {
        const float fadeInPx = juce::jmin (bodyWidth,
                                           (float)(fadeInSecs / (float)clipLenSecs) * bodyWidth);
        const float dotX = bodyLeft + fadeInPx * 0.5f;
        const float dotY = (bodyBottom + bodyTop) * 0.5f;
        if (std::hypot (px - dotX, py - dotY) < kCurveTolerance)
            return FadeDragType::FadeInCurve;
    }

    if (fadeOutSecs > 0.0f && clipLenSecs > 0.0)
    {
        const float fadeOutPx = juce::jmin (bodyWidth,
                                            (float)(fadeOutSecs / (float)clipLenSecs) * bodyWidth);
        const float dotX = bodyRight - fadeOutPx * 0.5f;
        const float dotY = (bodyBottom + bodyTop) * 0.5f;
        if (std::hypot (px - dotX, py - dotY) < kCurveTolerance)
            return FadeDragType::FadeOutCurve;
    }

    // Check corner handles (12×12 hit area)
    constexpr float kHandleSize = 12.0f;
    if (px >= bodyLeft && px <= bodyLeft + kHandleSize &&
        py >= bodyTop  && py <= bodyTop  + kHandleSize)
        return FadeDragType::FadeIn;

    if (px >= bodyRight - kHandleSize && px <= bodyRight &&
        py >= bodyTop                 && py <= bodyTop + kHandleSize)
        return FadeDragType::FadeOut;

    return FadeDragType::None;
}

juce::Rectangle<float> ClipComponent::getGainHandleRect() const
{
    constexpr float kHandleSize = 8.0f;
    const float cx = (float)getWidth() * 0.5f;
    // Top-center of the header band (y=0 = top of clip component)
    return { cx - kHandleSize * 0.5f, 0.0f, kHandleSize, kHandleSize };
}

// Trim zone helpers — bottom half of left/right 8px edge.
// Only bottom half: top half is occupied by fade corner handles.
// Only 8px wide: centre area stays as Click-Zone / Marquee.
juce::Rectangle<float> ClipComponent::getLeftTrimZone() const
{
    constexpr float kTrimWidth = 8.0f;
    const float halfH = (float)getHeight() * 0.5f;
    return { 0.0f, halfH, kTrimWidth, halfH };
}

juce::Rectangle<float> ClipComponent::getRightTrimZone() const
{
    constexpr float kTrimWidth = 8.0f;
    const float halfH = (float)getHeight() * 0.5f;
    return { (float)getWidth() - kTrimWidth, halfH, kTrimWidth, halfH };
}

//==============================================================================

juce::Rectangle<float> ClipComponent::takesBadgeBounds() const
{
    // Dieselbe Geometrie wie in paint() — deshalb hier als EINE Quelle, sonst
    // driften Zeichnung und Trefferflaeche auseinander.
    constexpr float badgeW = 30.0f;
    constexpr float headerH = 16.0f;
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    return { bounds.getRight() - 1.0f - badgeW, bounds.getY() + 2.0f,
             badgeW - 2.0f, headerH - 4.0f };
}

void ClipComponent::showTakesMenu()
{
    // Take-Wechsel DIREKT am Clip — so macht es Logic („click the Take Folder
    // name to open a menu of takes and comps"), und so hat es der Anwender
    // erwartet: bis 2026-08-18 fuehrte der einzige Weg ueber Rechtsklick, und
    // das Badge war reine Anzeige.
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);

    if (waveClip == nullptr)
        return;

    const int total = waveClip->getNumTakes (false);

    if (total <= 1)
        return;

    const int active   = clipEditor.getActiveTake (*waveClip);
    const bool comped  = CompingEngine::isCompActive (*waveClip);
    const bool savedComp = waveClip->state.getChildWithName (CompingEngine::compType).isValid();

    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("Aufnahme ausw\xc3\xa4hlen"));

    for (int i = 0; i < total; ++i)
        m.addItem (100 + i, "Take " + juce::String (i + 1) + " von " + juce::String (total),
                   true, ! comped && i == active);

    if (savedComp)
    {
        m.addSeparator();
        m.addItem (1, juce::String::fromUTF8 ("Zusammengesetzt (Comp)"), true, comped);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [this, safe = juce::Component::SafePointer<ClipComponent> (this)] (int r)
    {
        if (safe == nullptr || r == 0)
            return;

        if (r >= 100)
            clipEditor.setActiveTake (clip, r - 100);
        else if (r == 1)
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
                CompingEngine::selectComp (*wc);
    });
}

void ClipComponent::mouseDown (const juce::MouseEvent& e)
{
    // ---- Defensive: clean up leaked unbounded-mouse state from a prior interaction ----
    // If a previous fade drag ended without calling mouseUp (e.g. focus loss), JUCE's
    // internal unbounded-movement flag may still be true.  Resetting it here prevents
    // accumulated cursor-offset errors from poisoning the next drag.
    if (activeFadeDrag != FadeDragType::None)
    {
        const_cast<juce::MouseInputSource&> (e.source).enableUnboundedMouseMovement (false, false);
        activeFadeDrag = FadeDragType::None;
    }

    // Klick auf das Takes-Badge oeffnet die Take-Auswahl (Logic-Verhalten) und
    // startet KEINEN Clip-Drag — sonst verschoebe jeder Menueaufruf den Clip.
    if (e.mods.isLeftButtonDown() && ! takesBadgeHit.isEmpty()
        && takesBadgeHit.contains (e.getPosition()))
    {
        showTakesMenu();
        return;
    }

    // ---- Eraser tool (Studio One parity): a left-click anywhere on the clip deletes it.
    // Checked before handle/drag logic so it wins regardless of where you click. deleteClip
    // triggers a rebuild that deletes THIS component, so return immediately and touch nothing. ----
    if (activeTool_ == EditTool::Eraser && e.mods.isLeftButtonDown())
    {
        clipEditor.deleteClip (clip);
        return;
    }

    // ---- Mute tool (Studio One parity): a left-click toggles the clip's mute state.
    // I-03: TE clip mute lives in the state property — write through the UndoManager
    // so every click is one undo step (same mechanic as the dock editor's Mute button). ----
    if (activeTool_ == EditTool::Mute && e.mods.isLeftButtonDown())
    {
        auto& um = clip.edit.getUndoManager();
        um.beginNewTransaction ("Clip-Mute");
        const bool muted = (bool) clip.state.getProperty (te::IDs::mute, false);
        clip.state.setProperty (te::IDs::mute, ! muted, &um);
        repaint();
        return;
    }

    // ---- Fade handle hit test (audio clips only, all tools) ----
    // Must be checked BEFORE any existing drag logic so fade drags don't
    // accidentally start a clip move.  Fade handles have highest priority —
    // they win regardless of which tool is active.
    if (e.mods.isLeftButtonDown())
    {
        const FadeDragType hit = getFadeHandleAt (e.getPosition());
        if (hit != FadeDragType::None)
        {
            activeFadeDrag   = hit;
            fadeDragStartX   = e.x;
            if (hit == FadeDragType::FadeIn)
            {
                fadeDragStartValue = (float)(double)clip.state.getProperty (te::IDs::fadeIn,  0.0);
            }
            else if (hit == FadeDragType::FadeOut)
            {
                fadeDragStartValue = (float)(double)clip.state.getProperty (te::IDs::fadeOut, 0.0);
            }
            else if (hit == FadeDragType::FadeInCurve)
            {
                // For curve drags we store start-Y in fadeDragStartX (repurposed field)
                fadeDragStartX = e.y;
            }
            else if (hit == FadeDragType::FadeOutCurve)
            {
                fadeDragStartX = e.y;
            }
            // Use the correct cursor for each handle type.
            // enableUnboundedMouseMovement for curve drags so vertical movement is
            // not constrained by screen edges — same as Logic Pro / Pro Tools sliders.
            if (hit == FadeDragType::FadeInCurve || hit == FadeDragType::FadeOutCurve)
            {
                setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
                const_cast<juce::MouseInputSource&> (e.source).enableUnboundedMouseMovement (true, false);
            }
            else
            {
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                const_cast<juce::MouseInputSource&> (e.source).enableUnboundedMouseMovement (true, false);
            }
            return;
        }
    }

    // Right-click: show clip context menu
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Duplicate");
        menu.addItem (2, "Delete");
        menu.addSeparator();
        menu.addItem (3, "Copy");
        menu.addItem (4, "Split at Playhead");
        {
            // Merge/"verkleben" needs 2+ selected clips including this one — the
            // engine enforces same-track/same-type, the menu only gates on count.
            auto* avMerge = findParentComponentOfClass<ArrangementView>();
            const bool canMerge = avMerge != nullptr
                                  && avMerge->isClipSelected (this)
                                  && avMerge->getNumSelectedClips() >= 2;
            menu.addItem (12, juce::String::fromUTF8 ("Events verbinden (\xe2\x8c\x98J)"), canMerge);
        }
        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            menu.addItem (6, juce::String::fromUTF8 ("Tempo aus Clip erkennen…"));

            // P7a (S1 "Extract to tempo track"): Grid folgt der Performance.
            // Bei aktivem Timestretch (autoTempo) ist Datei-Zeit != Edit-Zeit → aus.
            menu.addItem (10, juce::String::fromUTF8 ("Tempo-Map aus Clip extrahieren…"),
                          ! wc->getAutoTempo());

            // Timestretch (Phase 3, Task 0): follow / stop following the song tempo.
            // Item 7 is a toggle — checked while the clip is conformed (autoTempo on).
            if (wc->getAutoTempo())
                menu.addItem (7, juce::String::fromUTF8 ("Songtempo-Anpassung entfernen"), true, true);
            else
                menu.addItem (7, juce::String::fromUTF8 ("An Songtempo anpassen…"));

            // P3 (S1 "Bounce/Transform"): render the event to a new file in place
            menu.addSeparator();
            menu.addItem (8, "Bounce in Place (mit Insert-FX)");
            menu.addItem (9, "Bounce in Place (nur Event)");

            // P8: Vocal-Tuning (offline, Bounce-Weg) — nur bei Datei-Zeit == Edit-Zeit
            menu.addItem (11, juce::String::fromUTF8 ("Vocal-Tuning\xe2\x80\xa6"),
                          ! wc->getAutoTempo());
        }

        // Separate recording passes remain intact; choose which overlapping clip wins.
        juce::Array<te::EditItemID> recordingChoices;
        if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr)
            if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
            {
                juce::PopupMenu recordings;
                juce::int64 highestPriority = 0;
                for (auto* candidate : track->getClips())
                    if (candidate->getPosition().time.overlaps (clip.getPosition().time))
                        highestPriority = std::max (highestPriority, AizanaEngineBehaviour::recordingPriority (*candidate));
                for (auto* candidate : track->getClips())
                    if (dynamic_cast<te::WaveAudioClip*> (candidate) != nullptr
                        && candidate->getPosition().time.overlaps (clip.getPosition().time))
                    {
                        recordingChoices.add (candidate->itemID);
                        recordings.addItem (10000 + recordingChoices.size() - 1,
                            juce::String (recordingChoices.size()) + ": " + candidate->getName(), true,
                            highestPriority > 0 && AizanaEngineBehaviour::recordingPriority (*candidate) == highestPriority);
                    }
                if (recordingChoices.size() > 1)
                    menu.addSubMenu (juce::String::fromUTF8 ("Aufnahme bevorzugen"), recordings);
            }

        // Takes submenu: shown for WaveAudioClip when loop recording produced multiple takes
        int totalTakes = 0;
        int activeTake = 0;
        if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            totalTakes = waveClip->getNumTakes (false);
            activeTake = clipEditor.getActiveTake (*waveClip);
        }
        if (totalTakes > 1)
        {
            juce::PopupMenu takesSubMenu;
            for (int i = 0; i < totalTakes; ++i)
            {
                const bool isCurrent = (i == activeTake);
                takesSubMenu.addItem (100 + i, "Take " + juce::String (i + 1), true, isCurrent);
            }
            menu.addSeparator();
            menu.addSubMenu ("Select Take", takesSubMenu);
            menu.addItem (5, juce::String::fromUTF8 ("Comping-Editor öffnen…"));
        }

        // Event-FX submenu (Studio One: inserts on the EVENT, not the track).
        // TE plays a clip's own PluginList live as part of the clip node.
        juce::Array<te::Plugin*> clipFx;
        if (auto* clipPlugins = clip.getPluginList())
            for (auto* p : clipPlugins->getPlugins())
            {
                const juce::String t = p->getPluginType();
                if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
                    clipFx.add (p);
            }
        {
            juce::PopupMenu fxMenu;
            fxMenu.addItem (399, juce::String::fromUTF8 ("FX hinzufügen…"));
            if (! clipFx.isEmpty())
                fxMenu.addSeparator();
            for (int i = 0; i < clipFx.size(); ++i)
            {
                juce::PopupMenu one;
                one.addItem (400 + i, juce::String::fromUTF8 ("Öffnen"));
                one.addItem (500 + i, "Entfernen");
                fxMenu.addSubMenu (clipFx[i]->getName(), one);
            }
            menu.addSeparator();
            menu.addSubMenu ("Event-FX", fxMenu);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this, clipFx, recordingChoices, safe = juce::Component::SafePointer<ClipComponent> (this)] (int result)
            {
                if (safe == nullptr) return;
                if (result >= 10000 && result < 10000 + recordingChoices.size())
                {
                    if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
                        for (auto* candidate : track->getClips())
                            if (candidate->itemID == recordingChoices[result - 10000])
                            {
                                clip.edit.getUndoManager().beginNewTransaction ("Aufnahme bevorzugen");
                                AizanaEngineBehaviour::preferRecording (*candidate);
                                break;
                            }
                    return;
                }
                // ---- Event-FX ----
                if (result == 399)
                {
                    if (auto* av = findParentComponentOfClass<ArrangementView>())
                        PluginBrowserPopup::showForComponent (
                            *this,
                            av->getPluginScanner().getKnownPlugins(),
                            [this, av] (const juce::PluginDescription& desc)
                            {
                                if (auto* fx = av->getPluginManager().insertPluginOnClip (clip, desc))
                                    av->getPluginWindowManager().openPluginWindow (*fx);
                                repaint();
                            });
                    return;
                }
                if (result >= 500 && result < 500 + clipFx.size())
                {
                    if (auto* av = findParentComponentOfClass<ArrangementView>())
                    {
                        auto* fx = clipFx[result - 500];
                        av->getPluginWindowManager().closePluginWindow (*fx);
                        clip.edit.getUndoManager().beginNewTransaction ("Remove event FX");
                        fx->removeFromParent();
                    }
                    repaint();
                    return;
                }
                if (result >= 400 && result < 400 + clipFx.size())
                {
                    if (auto* av = findParentComponentOfClass<ArrangementView>())
                        av->getPluginWindowManager().openPluginWindow (*clipFx[result - 400]);
                    return;
                }

                if (result == 1)
                {
                    clipEditor.duplicateClip (clip);
                }
                else if (result == 2)
                {
                    // Multi-selection aware: if this clip is part of a selection,
                    // "Delete" removes the WHOLE selection in one undo step —
                    // right-click → Delete on 3 selected snippets deletes all 3.
                    auto* av = findParentComponentOfClass<ArrangementView>();
                    if (av != nullptr && av->isClipSelected (this) && av->getNumSelectedClips() > 1)
                        av->deleteSelectedClips();
                    else
                        clipEditor.deleteClip (clip);
                }
                else if (result == 3)
                {
                    clipEditor.copyToClipboard (clip);
                }
                else if (result == 4)
                {
                    // Split at current playhead position
                    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
                    {
                        const double playheadSecs =
                            clip.edit.getTransport().getPosition().inSeconds();
                        clipEditor.cutClip (*waveClip, playheadSecs);
                    }
                }
                else if (result == 5)
                {
                    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
                        if (auto* mainComp = findParentComponentOfClass<MainComponent>())
                            mainComp->showCompingEditor (*waveClip);
                }
                else if (result == 6)
                {
                    detectTempoFromClip();
                }
                else if (result == 7)
                {
                    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
                    {
                        if (wc->getAutoTempo())
                            removeSongTempoConform();
                        else
                            conformClipToSongTempo();
                    }
                }
                else if (result == 8 || result == 9)
                {
                    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
                        clipEditor.bounceClipInPlace (*wc, result == 8);
                }
                else if (result == 10)
                {
                    extractTempoMapFromClip();
                }
                else if (result == 11)
                {
                    if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr)
                        showVocalTuningDialog();
                }
                else if (result == 12)
                {
                    // Merge rebuilds the track rows and destroys THIS component —
                    // hand off to the view and touch nothing afterwards.
                    if (auto* av = findParentComponentOfClass<ArrangementView>())
                        av->mergeSelectedClips();
                    return;
                }
                else if (result >= 100)
                {
                    clipEditor.setActiveTake (clip, result - 100);
                }
            });
        return;
    }

    if (! e.mods.isLeftButtonDown()) return;

    // Gain handle hit-test (WaveAudioClip only, min width guard)
    // Placed AFTER all fade hit-tests, BEFORE clip drag logic.
    if (!gainDragActive
        && dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr
        && getWidth() >= 60
        && getGainHandleRect().contains (e.position))
    {
        // Cmd+Click = reset to 0 dB immediately
        if (e.mods.isCommandDown())
        {
            if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
                wave->state.setProperty (te::IDs::gain, 0.0,
                                         &wave->edit.getUndoManager());
            repaint();
            return;
        }

        gainDragActive  = true;
        gainDragStartY  = (float)e.position.y;
        gainDragStartDb = currentGainDb;
        return;
    }

    // Trim handle hit-test — Pointer mode only, WaveAudioClip only.
    // Placed AFTER all fade hit-tests and AFTER gain hit-test, BEFORE Click Zone,
    // so trim wins at the bottom-half edges and Click Zone wins in the centre area.
    if (activeTrimSide == TrimSide::None
        && activeTool_ == EditTool::Pointer
        && dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr)
    {
        const bool hitLeft  = getLeftTrimZone().contains (e.position);
        const bool hitRight = getRightTrimZone().contains (e.position);

        if (hitLeft || hitRight)
        {
            activeTrimSide      = hitLeft ? TrimSide::Left : TrimSide::Right;
            trimDragStartX      = e.getScreenPosition().x;
            trimDragStartSecs   = clip.getPosition().getStart().inSeconds();
            trimDragStartEnd    = clip.getPosition().getEnd().inSeconds();
            trimDragStartOffset = clip.getPosition().getOffset().inSeconds();

            // Left extension limit: offset can never go below 0
            trimMaxExtendLeft  = trimDragStartSecs - trimDragStartOffset;
            // Right extension limit: source file end (thumbnail->getTotalLength() = full source duration)
            const double sourceLen = thumbnailLoaded ? thumbnail->getTotalLength() : 3600.0;
            trimMaxExtendRight = trimDragStartSecs + (sourceLen - trimDragStartOffset);

            // Begin a single undo transaction — all setPosition() calls during drag
            // append to this transaction so Cmd+Z undoes the whole trim in one step.
            editManager.getUndoManager().beginNewTransaction ("Trim clip");
            return;
        }
    }

    // Scissors tool: forward click for clip splitting.
    if (activeTool_ == EditTool::Scissors)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
        {
            const auto eventInAV   = e.getEventRelativeTo (av);
            const int  timelineLeft = ArrangementView::kHeaderWidth;
            const int  xInTimeline  = eventInAV.x - timelineLeft
                                      + av->getTimelineViewportX();
            const double timeSecs = juce::jmax (0.0, (double)xInTimeline / av->getPixelsPerSecond());
            av->splitClipAtTime (timeSecs, eventInAV.y);
        }
        return;
    }

    // Marquee / RangeSelect tools: forward to ArrangementView for range selection.
    // ClipComponent has the mouse capture after this, so mouseDrag/mouseUp also
    // forward to ArrangementView to keep the range dragging alive.
    if (activeTool_ == EditTool::Marquee || activeTool_ == EditTool::RangeSelect)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseDown (e.getEventRelativeTo (av));
        return;
    }

    // Click Zone: lower half of clip in Pointer mode activates temporary Marquee.
    if (activeTool_ == EditTool::Pointer && e.y > getHeight() / 2)
    {
        inMarqueeZoneDrag = true;
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->startRangeSelectionFromClickZone (e.getEventRelativeTo (av));
        return;
    }

    // Normal pointer tool — notify ArrangementView to select this clip, then start drag
    if (e.mods.isCommandDown())
    {
        // Cmd+click: add/remove from selection without clearing others
        if (onCmdSelectRequest)
            onCmdSelectRequest (this);
        return;  // no drag on Cmd+click
    }
    if (onSelectRequest)
        onSelectRequest (this);

    // Begin one undo transaction for the whole drag — all mouseDrag position
    // updates share it so Cmd+Z undoes the full move in a single step.
    clipEditor.beginMoveTransaction();

    dragStartClipTimeSecs = clip.getPosition().getStart().inSeconds();
    dragStartMouseX = e.getEventRelativeTo (getParentComponent()).x;

    // Notify ArrangementView to snapshot initial positions of all selected clips.
    if (onDragWillStart)
        onDragWillStart (this);
    // Record Y in timelineContainer (grandparent of clipArea) for inter-track detection
    auto* container = getParentComponent() != nullptr ? getParentComponent()->getParentComponent() : nullptr;
    dragStartMouseY = container != nullptr ? e.getEventRelativeTo (container).y : e.getScreenY();
    isDragging = false;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ClipComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;

    // ---- Gain drag — checked before fade drag to ensure mutual exclusion ----
    if (gainDragActive)
    {
        // Sensitivity: 1px up = +0.5 dB. Full range (-96..+24) = 240px drag.
        const float deltaPx = gainDragStartY - (float)e.position.y; // up = positive = louder
        const float newDb   = juce::jlimit (-96.0f, 24.0f, gainDragStartDb + deltaPx * 0.5f);
        currentGainDb = newDb;
        repaint(); // live visual update during drag
        return;
    }

    // ---- Trim drag — after gain drag, before fade drag; all three are mutually exclusive ----
    if (activeTrimSide != TrimSide::None)
    {
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
        {
            // Convert screen-pixel delta to timeline seconds using current zoom level.
            const double deltaSecs = (e.getScreenPosition().x - trimDragStartX) / pixelsPerSecond;

            if (activeTrimSide == TrimSide::Left)
            {
                // Drag right = shorten from left (offset grows), drag left = extend (offset shrinks).
                const double newStart = juce::jlimit (
                    trimMaxExtendLeft,          // offset can't go negative
                    trimDragStartEnd - 0.05,    // min 50ms clip length
                    trimDragStartSecs + deltaSecs
                );
                // Offset adjusts so audio stays in sync: every second the start moves right,
                // one more second of source audio is consumed.
                const double newOffset = trimDragStartOffset + (newStart - trimDragStartSecs);
                wave->setPosition ({ TimeRng (TimePos::fromSeconds (newStart),
                                              TimePos::fromSeconds (trimDragStartEnd)),
                                     TimeDur::fromSeconds (newOffset) });

                // Update visual bounds immediately (async resized() will agree)
                const int newX = (int)(newStart * pixelsPerSecond);
                const int newW = juce::jmax (4, (int)((trimDragStartEnd - newStart) * pixelsPerSecond));
                setBounds (newX, getY(), newW, getHeight());
            }
            else  // TrimSide::Right
            {
                // Drag left = shorten from right, drag right = extend.
                const double newEnd = juce::jlimit (
                    trimDragStartSecs + 0.05,   // min 50ms clip length
                    trimMaxExtendRight,          // can't extend past source file end
                    trimDragStartEnd + deltaSecs
                );
                // Start and offset are fixed — only the end point moves.
                wave->setPosition ({ TimeRng (TimePos::fromSeconds (trimDragStartSecs),
                                              TimePos::fromSeconds (newEnd)),
                                     TimeDur::fromSeconds (trimDragStartOffset) });

                const int newX = (int)(trimDragStartSecs * pixelsPerSecond);
                const int newW = juce::jmax (4, (int)((newEnd - trimDragStartSecs) * pixelsPerSecond));
                setBounds (newX, getY(), newW, getHeight());
            }
            repaint();
        }
        return;
    }

    // ---- Fade drag handling — checked before any tool routing ----
    // An active fade drag takes priority over all tools (including Scissors).
    // activeFadeDrag is set in mouseDown when a fade handle is hit.
    if (activeFadeDrag != FadeDragType::None)
    {
        const double clipLenSecs = clip.getPosition().getLength().inSeconds();
        const float  bodyWidth   = (float)getWidth() - 2.0f; // approximate usable width

        if (activeFadeDrag == FadeDragType::FadeIn)
        {
            // Rightward drag increases fade-in duration
            const float deltaPx   = (float)(e.x - fadeDragStartX);
            const float deltaSecs = deltaPx / (float)pixelsPerSecond;
            const float newVal    = juce::jlimit (0.0f, (float)clipLenSecs,
                                                  fadeDragStartValue + deltaSecs);
            clip.state.setProperty (te::IDs::fadeIn, (double)newVal,
                                    &clip.edit.getUndoManager());
            repaint();
        }
        else if (activeFadeDrag == FadeDragType::FadeOut)
        {
            // Leftward drag increases fade-out duration (negative delta grows it)
            const float deltaPx   = (float)(fadeDragStartX - e.x);
            const float deltaSecs = deltaPx / (float)pixelsPerSecond;
            const float newVal    = juce::jlimit (0.0f, (float)clipLenSecs,
                                                  fadeDragStartValue + deltaSecs);
            clip.state.setProperty (te::IDs::fadeOut, (double)newVal,
                                    &clip.edit.getUndoManager());
            repaint();
        }
        else if (activeFadeDrag == FadeDragType::FadeInCurve
              || activeFadeDrag == FadeDragType::FadeOutCurve)
        {
            // Continuous curve: drag up → logarithmic (+1.0), center → linear (0.0),
            // drag down → exponential (-1.0).  50px of drag = full range.
            // fadeDragStartX holds the start-Y for curve drags (repurposed field — see mouseDown).
            const float curveValue = juce::jlimit (-1.0f, 1.0f,
                (float)(fadeDragStartX - e.y) / 50.0f);

            // Write curve type to TE for audio processing (integer 0/1/2).
            // Threshold 0.15 gives a dead-band around center so linear is easy to hit.
            const int curveType = (curveValue > 0.15f) ? 1 : (curveValue < -0.15f) ? 2 : 0;

            if (activeFadeDrag == FadeDragType::FadeInCurve)
            {
                fadeInCurve = curveValue;   // float used by drawFadeOverlay for smooth visual
                clip.state.setProperty (te::IDs::fadeInType,  curveType,
                                        &clip.edit.getUndoManager());
            }
            else
            {
                fadeOutCurve = curveValue;
                clip.state.setProperty (te::IDs::fadeOutType, curveType,
                                        &clip.edit.getUndoManager());
            }
            repaint();  // redraws the bezier fade line live on every drag event
        }
        return;  // do NOT fall through to clip move logic
    }

    // Scissors tool: no drag behaviour (click-only for splitting)
    if (activeTool_ == EditTool::Scissors) return;

    // Mute tool: click-only (mouseDown toggled the mute) — never start a clip move.
    if (activeTool_ == EditTool::Mute) return;

    // Click Zone Marquee drag — forward to ArrangementView.
    if (inMarqueeZoneDrag)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseDrag (e.getEventRelativeTo (av));
        return;
    }

    // Marquee/RangeSelect: forward drag to ArrangementView for range extension.
    if (activeTool_ == EditTool::Marquee || activeTool_ == EditTool::RangeSelect)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseDrag (e.getEventRelativeTo (av));
        return;
    }

    isDragging = true;

    const int currentX = e.getEventRelativeTo (getParentComponent()).x;
    const double rawDelta = (double)(currentX - dragStartMouseX) / pixelsPerSecond;

    // Snap the ABSOLUTE clip position to the nearest grid line (Studio One / Logic Pro X standard).
    // snapTimeToGrid(dragStart + rawDelta) evaluates the clip's actual timeline position
    // and snaps it to the nearest grid boundary — NOT the delta from the drag origin.
    // The previous "snap the delta" approach caused inter-track drag overshoot: a 0.26 s
    // horizontal drift during a vertical drag would round the 0.26 s delta up to the next
    // 0.5 s grid multiple, jumping the clip 0.5 s right instead of landing at 2.5 s.
    // With absolute snapping: 2.37 s + 0.26 s drift → snapTimeToGrid(2.63) = 2.5 s. Correct.
    // Alt bypasses snap and uses the raw unsnapped position.
    auto* av = findParentComponentOfClass<ArrangementView>();
    const double rawAbsolute  = dragStartClipTimeSecs + rawDelta;
    const double newStartSecs = juce::jmax (0.0,
        (av != nullptr) ? av->snapTimeToGrid (rawAbsolute, e.mods.isAltDown()) : rawAbsolute);
    const double snappedDelta = newStartSecs - dragStartClipTimeSecs;

    // Move clip position (undoable via ClipEditor)
    clipEditor.moveClip (clip, newStartSecs);

    // Update visual position immediately
    const double endSecs = newStartSecs + clip.getPosition().getLength().inSeconds();
    const int newX = (int)(newStartSecs * pixelsPerSecond);
    const int newW = juce::jmax (4, (int)((endSecs - newStartSecs) * pixelsPerSecond));
    setBounds (newX, getY(), newW, getHeight());

    // Move all other selected clips by the same snapped delta (group drag).
    if (onMultiClipDrag)
        onMultiClipDrag (clip, snappedDelta);

    // Inter-track drag: pass absolute Y in timelineContainer to ArrangementView.
    // Ghost/Proxy approach: ArrangementView only updates the ghost state here — no TE
    // data movement and no reparenting during drag. Actual move happens once on mouseUp.
    if (onInterTrackDrag)
    {
        // Hierarchy: ClipComponent → clipArea → TrackRow → timelineContainer
        auto* clipAreaComp     = getParentComponent();
        auto* trackRowComp     = clipAreaComp  ? clipAreaComp->getParentComponent()  : nullptr;
        auto* timelineContComp = trackRowComp  ? trackRowComp->getParentComponent()  : nullptr;
        if (timelineContComp != nullptr)
        {
            const int absY = e.getEventRelativeTo (timelineContComp).y;

            // Pass newStartSecs — the snapped absolute position the ghost should show.
            // Since no track crossing happens during drag, dragStartMouseX and
            // dragStartClipTimeSecs remain valid for the entire drag without reset.
            onInterTrackDrag (clip, newStartSecs, absY, this);
        }
    }
}

void ClipComponent::mouseUp (const juce::MouseEvent& e)
{
    // ---- Reset fade drag ----
    if (activeFadeDrag != FadeDragType::None)
    {
        // Release unbounded mouse movement that was enabled in mouseDown.
        // CRITICAL: call setScreenPosition AFTER enableUnboundedMouseMovement(false) —
        // JUCE would otherwise snap the cursor back to the mouseDown origin, causing a
        // visible jump after every fade curve drag.
        const_cast<juce::MouseInputSource&> (e.source).enableUnboundedMouseMovement (false, false);
        juce::Desktop::getInstance().getMainMouseSource()
            .setScreenPosition (e.getScreenPosition().toFloat());
        activeFadeDrag = FadeDragType::None;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    // ---- Gain drag end — persist final value to TE ValueTree with undo ----
    if (gainDragActive)
    {
        gainDragActive = false;
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&clip))
            wave->state.setProperty (te::IDs::gain,
                                     (double)currentGainDb,
                                     &wave->edit.getUndoManager());
        repaint();
        return;
    }

    // ---- Trim drag end — the TE ValueTree was already updated live during drag.
    //      The single transaction opened in mouseDown covers all intermediate changes. ----
    if (activeTrimSide != TrimSide::None)
    {
        activeTrimSide = TrimSide::None;
        repaint();
        return;
    }

    // Click Zone Marquee — forward release to ArrangementView, then reset.
    if (inMarqueeZoneDrag)
    {
        inMarqueeZoneDrag = false;
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseUp (e.getEventRelativeTo (av));
        return;
    }

    if (activeTool_ == EditTool::Marquee || activeTool_ == EditTool::RangeSelect)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseUp (e.getEventRelativeTo (av));
        return;
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
    isDragging = false;

    // If a ghost inter-track drag was active, commit the TE move now (mouseUp = single
    // data operation).  commitInterTrackDrag sets pendingRebuild = true on this component
    // so the deferred rebuild fires via the existing pendingRebuild → onDragEnded path.
    if (isGhostDimmed)
    {
        isGhostDimmed = false;
        repaint();  // un-dim immediately so there is no visual flash on rebuild
        if (onInterTrackDragCommit)
            onInterTrackDragCommit();
    }

    // If a cross-track move happened (either via ghost commit above or other path),
    // trigger a full rebuild now that the drag is complete.  The rebuild is deferred
    // via callAsync so it runs after this mouseUp returns — the component remains
    // valid for the rest of this call.
    if (pendingRebuild)
    {
        pendingRebuild = false;
        if (onDragEnded)
            onDragEnded();
    }
}

void ClipComponent::mouseMove (const juce::MouseEvent& e)
{
    // Fade handles take cursor priority over all tools — checked unconditionally.
    // JUCE only re-polls getMouseCursor() on component entry or after updateMouseCursor(),
    // so setMouseCursor here keeps per-zone feedback live on every move event.
    const FadeDragType hit = getFadeHandleAt (e.getPosition());
    if (hit == FadeDragType::FadeIn || hit == FadeDragType::FadeOut)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (hit == FadeDragType::FadeInCurve || hit == FadeDragType::FadeOutCurve)
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr
             && getWidth() >= 60
             && getGainHandleRect().contains (e.position))
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (activeTool_ == EditTool::Pointer
             && dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr
             && (getLeftTrimZone().contains (e.position)
                 || getRightTrimZone().contains (e.position)))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (activeTool_ == EditTool::Pointer)
        setMouseCursor (e.y > getHeight() / 2
                        ? juce::MouseCursor::CrosshairCursor
                        : juce::MouseCursor::NormalCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

juce::MouseCursor ClipComponent::getMouseCursor()
{
    const auto pos = getMouseXYRelative();
    // Fade handles have highest cursor priority — active in all tool modes.
    const FadeDragType hit = getFadeHandleAt (pos);
    if (hit == FadeDragType::FadeIn || hit == FadeDragType::FadeOut)
        return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    if (hit == FadeDragType::FadeInCurve || hit == FadeDragType::FadeOutCurve)
        return juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr
        && getWidth() >= 60
        && getGainHandleRect().contains (pos.toFloat()))
        return juce::MouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (activeTool_ == EditTool::Pointer
        && dynamic_cast<te::WaveAudioClip*> (&clip) != nullptr
        && (getLeftTrimZone().contains (pos.toFloat()) || getRightTrimZone().contains (pos.toFloat())))
        return juce::MouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    if (activeTool_ == EditTool::Pointer && pos.y > getHeight() / 2)
        return juce::MouseCursor (juce::MouseCursor::CrosshairCursor);
    return juce::MouseCursor (juce::MouseCursor::NormalCursor);
}

void ClipComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Mute tool: both clicks of a fast double-click already toggled in mouseDown —
    // never fall through to Piano Roll / range split.
    if (activeTool_ == EditTool::Mute)
        return;

    // Marquee OR Pointer tool on an audio clip: double-click splits at active range boundaries.
    // Pointer tool is included so the workflow "draw range → switch to Pointer →
    // double-click inside range" works without needing to stay in Marquee mode.
    // MIDI clips are excluded — their double-click always opens Piano Roll.
    if ((activeTool_ == EditTool::Marquee || activeTool_ == EditTool::Pointer)
        && dynamic_cast<te::MidiClip*> (&clip) == nullptr)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseDoubleClick (e.getEventRelativeTo (av));
        return;
    }

    // Double-click on a MIDI clip -> open Piano Roll in docked bottom panel
    if (auto* midiClip = dynamic_cast<te::MidiClip*> (&clip))
    {
        // The lower click zone starts a range without selecting. The dock follows
        // selection, so explicitly select before opening it for this clip.
        if (onSelectRequest) onSelectRequest (this);
        if (auto* mainComp = findParentComponentOfClass<MainComponent>())
            mainComp->showPianoRoll (*midiClip);
    }
    // Audio clips: no action in Phase 1 (future: waveform editor)
}

//==============================================================================
void ClipComponent::detectTempoFromClip()
{
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (waveClip == nullptr)
        return;

    const auto file = waveClip->getSourceFileReference().getFile();
    if (! file.existsAsFile())
        return;

    // Analysis runs on a worker thread; result arrives on the message thread.
    TempoDetector::detectAsync (file,
        [safe = juce::Component::SafePointer<ClipComponent> (this)] (double bpm)
        {
            if (safe == nullptr)
                return;

            if (bpm <= 0.0)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "Tempo-Erkennung",
                    juce::String::fromUTF8 ("Kein eindeutiges Tempo erkennbar "
                                            "(zu kurz, zu leise oder unrhythmisch)."));
                return;
            }

            const auto rounded = std::round (bpm * 10.0) / 10.0;
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Tempo-Erkennung",
                juce::String::fromUTF8 ("Geschätztes Tempo: ")
                    + juce::String (rounded, 1) + " BPM\n\n"
                    + juce::String::fromUTF8 ("Song-Tempo übernehmen?"),
                juce::String::fromUTF8 ("Übernehmen"), "Abbrechen", nullptr,
                juce::ModalCallbackFunction::create (
                    [safe, rounded] (int result)
                    {
                        if (safe == nullptr || result == 0)
                            return;
                        auto& edit = safe->clip.edit;
                        edit.getUndoManager().beginNewTransaction ("Set tempo from clip");
                        edit.tempoSequence.getTempo (0)->setBpm (
                            juce::jlimit (30.0, 250.0, rounded));
                    }));
        });
}

void ClipComponent::extractTempoMapFromClip()
{
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (waveClip == nullptr || waveClip->getAutoTempo())
        return;

    const auto file = waveClip->getSourceFileReference().getFile();
    if (! file.existsAsFile())
        return;

    // Beat-Tracking auf dem Worker; Anwendung auf dem Message-Thread (1 Undo).
    TempoDetector::detectBeatsWithConfidenceAsync (file,
        [safe = juce::Component::SafePointer<ClipComponent> (this)]
        (TempoDetector::BeatResult r)
        {
            if (safe == nullptr)
                return;

            if (r.beats.size() < 8)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "Tempo-Map",
                    juce::String::fromUTF8 ("Kein stabiler Puls erkennbar — "
                                            "Tempo-Map nicht extrahiert."));
                return;
            }

            // C2: ein Ergebnis, das nach Messung aussieht und falsch ist, ist
            // schlimmer als gar keins. Unter der Schwelle wird gewarnt statt
            // stillschweigend übernommen — mit der Ursache und dem Ausweg im
            // Klartext. Studio One lehnt hier ebenfalls ab („zu wenig Cadence"),
            // Cubase verlangt erkennbare Beats; erzwungen wird es aber nicht,
            // die Entscheidung bleibt beim Anwender.
            if (r.confidence < TempoDetector::kMinConfidence)
            {
                juce::String msg;
                msg << juce::String::fromUTF8 ("Der Puls ist auf dieser Spur nur schwach "
                                               "erkennbar (Verl\xc3\xa4sslichkeit ")
                    << juce::String (100.0 * r.confidence, 0)
                    << juce::String::fromUTF8 (" %, n\xc3\xb6tig sind ")
                    << juce::String (100.0 * TempoDetector::kMinConfidence, 0)
                    << juce::String::fromUTF8 (" %).\n\nNur unter ")
                    << juce::String (100.0 * r.onsetSupport, 0)
                    << juce::String::fromUTF8 (" % der gefundenen Beats liegt wirklich ein "
                                               "Anschlag. Das passiert auf d\xc3\xbcnn besetzten "
                                               "Spuren \xe2\x80\x94 eine Kick allein, ein einzelnes "
                                               "Instrument. Nimm daf\xc3\xbcr eine Spur mit "
                                               "durchgehendem Rhythmus: eine Schlagzeug-Summe "
                                               "oder den ganzen Mix.\n\nEine Tempo-Map aus diesem "
                                               "Material wird sehr wahrscheinlich falsch.");

                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::WarningIcon,
                    "Tempo-Map",
                    msg,
                    juce::String::fromUTF8 ("Trotzdem extrahieren"),
                    "Abbrechen",
                    nullptr,
                    juce::ModalCallbackFunction::create (
                        [safe, beats = r.beats] (int result)
                        {
                            if (result == 1 && safe != nullptr)
                                safe->applyTempoMap (beats);
                        }));
                return;
            }

            safe->applyTempoMap (r.beats);
        });
}

void ClipComponent::applyTempoMap (const juce::Array<double>& beats)
{
    auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (wc == nullptr)
        return;

    TempoEngine tempoEngine (editManager);
    const auto res = tempoEngine.extractTempoMapFromClip (*wc, beats);

    if (res.numPoints <= 0)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon, "Tempo-Map",
            juce::String::fromUTF8 ("Tempo-Map konnte nicht angewendet werden."));
        return;
    }

    juce::String msg;
    msg << juce::String (res.numPoints)
        << juce::String::fromUTF8 (" Tempo-Punkte gesetzt \xe2\x80\x94 das Raster folgt jetzt "
                                   "der Performance.\nGrundtempo ")
        << juce::String (res.leadInBpm, 1) << " BPM.";

    // C1: die Verschiebung sichtbar machen. Sie ist der Preis dafür, dass die
    // Anzeige vor dem Clip kein Fantasietempo mehr zeigt — verschwiegen wäre
    // sie eine Überraschung.
    if (std::abs (res.shiftSeconds) > 0.001)
        msg << juce::String::fromUTF8 ("\n\nDas Audio wurde um ")
            << juce::String (std::abs (res.shiftSeconds) * 1000.0, 0)
            << juce::String::fromUTF8 (" ms ")
            << (res.shiftSeconds > 0 ? juce::String::fromUTF8 ("nach hinten")
                                     : juce::String::fromUTF8 ("nach vorn"))
            << juce::String::fromUTF8 (" auf die n\xc3\xa4""chste Taktlinie ger\xc3\xbc""ckt "
                                       "(alle Clips gemeinsam, Abst\xc3\xa4nde bleiben).");

    msg << juce::String::fromUTF8 ("\n\nCmd+Z macht alles r\xc3\xbc""ckg\xc3\xa4ngig.");

    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon, "Tempo-Map", msg);
}

void ClipComponent::showVocalTuningDialog()
{
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (waveClip == nullptr || waveClip->getAutoTempo())
        return;

    // C3: Material VORHER vermessen. Die alte Fassung liess den Anwender erst
    // rechnen und meldete dann eine Sammelursache („kein tonales Material ODER
    // gestretcht ODER zu kurz") — damit war nicht feststellbar, was wirklich
    // zutraf. Jetzt steht die eine zutreffende Ursache im Dialog, bevor
    // irgendetwas passiert.
    VocalTuner probe (editManager);
    const auto analysis = probe.analyseClip (*waveClip);

    if (! analysis.isTunable())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            juce::String::fromUTF8 ("Vocal-Tuning nicht m\xc3\xb6glich"),
            analysis.describe());
        return;
    }

    auto* w = new juce::AlertWindow (juce::String::fromUTF8 ("Vocal-Tuning"),
                                     analysis.describe()
                                       + juce::String::fromUTF8 ("\n\nTonh\xc3\xb6he auf die Skala ziehen "
                                                                 "(offline, ersetzt den Clip \xe2\x80\x94 1\xc3\x97 Cmd+Z stellt das Original wieder her)."),
                                     juce::MessageBoxIconType::NoIcon);

    const juce::StringArray keys   { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray scales { "Chromatisch", "Dur", "Moll" };
    const juce::StringArray strengths { "100 % (voll)", "80 %", "50 % (sanft)" };
    const juce::StringArray speeds { juce::String::fromUTF8 ("Schnell (hart, 15 ms)"),
                                     juce::String::fromUTF8 ("Mittel (35 ms)"),
                                     juce::String::fromUTF8 ("Langsam (nat\xc3\xbcrlich, 80 ms)") };

    w->addComboBox ("scale", scales, "Skala");
    w->addComboBox ("key", keys, "Tonart (bei Dur/Moll)");
    w->addComboBox ("strength", strengths, juce::String::fromUTF8 ("St\xc3\xa4rke"));
    w->addComboBox ("speed", speeds, "Reaktion");
    w->getComboBoxComponent ("speed")->setSelectedItemIndex (1, juce::dontSendNotification);

    w->addButton ("Tunen", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<ClipComponent> (this), w] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owner (w);
                if (safe == nullptr || result != 1)
                    return;

                auto* wc = dynamic_cast<te::WaveAudioClip*> (&safe->clip);
                if (wc == nullptr)
                    return;

                VocalTuner::Settings st;
                const int scaleIdx = w->getComboBoxComponent ("scale")->getSelectedItemIndex();
                st.scale       = scaleIdx == 1 ? VocalTuner::Scale::major
                               : scaleIdx == 2 ? VocalTuner::Scale::minor
                                               : VocalTuner::Scale::chromatic;
                st.keyRootNote = juce::jmax (0, w->getComboBoxComponent ("key")->getSelectedItemIndex());
                const int sIdx = w->getComboBoxComponent ("strength")->getSelectedItemIndex();
                st.strength    = sIdx == 1 ? 0.8f : sIdx == 2 ? 0.5f : 1.0f;
                const int spIdx = w->getComboBoxComponent ("speed")->getSelectedItemIndex();
                st.smoothingMs = spIdx == 0 ? 15.0f : spIdx == 2 ? 80.0f : 35.0f;

                // v1 synchron (Analyse+Render); bei typischen Take-Längen kurz.
                VocalTuner tuner (safe->editManager);
                VocalTuner::Analysis outcome;
                if (tuner.tuneClipInPlace (*wc, st, &outcome) == nullptr)
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::InfoIcon,
                        "Vocal-Tuning", outcome.describe());
            }),
        false);
}

void ClipComponent::conformClipToSongTempo()
{
    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip);
    if (waveClip == nullptr)
        return;

    const auto file = waveClip->getSourceFileReference().getFile();
    if (! file.existsAsFile())
        return;

    // Estimate the clip's source tempo first (worker thread), then enable
    // auto-tempo so TE stretches the source to follow the song tempo map.
    TempoDetector::detectAsync (file,
        [safe = juce::Component::SafePointer<ClipComponent> (this)] (double bpm)
        {
            if (safe == nullptr)
                return;

            if (bpm <= 0.0)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    juce::String::fromUTF8 ("An Songtempo anpassen"),
                    juce::String::fromUTF8 ("Kein eindeutiges Clip-Tempo erkennbar "
                                            "(zu kurz, zu leise oder unrhythmisch)."));
                return;
            }

            const auto rounded = std::round (bpm * 10.0) / 10.0;
            auto& edit = safe->clip.edit;
            const auto clipStart = safe->clip.getPosition().getStart();
            const auto songBpm = edit.tempoSequence.getBpmAt (clipStart);

            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                juce::String::fromUTF8 ("An Songtempo anpassen"),
                juce::String::fromUTF8 ("Erkanntes Clip-Tempo: ")
                    + juce::String (rounded, 1) + " BPM\n"
                    + juce::String::fromUTF8 ("Songtempo: ")
                    + juce::String (songBpm, 1) + " BPM\n\n"
                    + juce::String::fromUTF8 ("Clip per Timestretch an das Songtempo "
                                              "anpassen? Der Clip folgt danach auch "
                                              "Tempo-Änderungen."),
                juce::String::fromUTF8 ("Anpassen"), "Abbrechen", nullptr,
                juce::ModalCallbackFunction::create (
                    [safe, rounded] (int result)
                    {
                        if (safe == nullptr || result == 0)
                            return;

                        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&safe->clip))
                        {
                            safe->clipEditor.conformToSongTempo (*wc, rounded);
                            safe->repaint();
                        }
                    }));
        });
}

void ClipComponent::removeSongTempoConform()
{
    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        clipEditor.removeSongTempoConform (*wc);
        repaint();
    }
}
