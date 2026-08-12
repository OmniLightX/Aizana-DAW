#include "TempoLane.h"

TempoLane::TempoLane (ApplicationState& as, TempoEngine& engine,
                      double& pps, double& scroll)
    : appState (as), tempoEngine (engine),
      pixelsPerSecond (pps), scrollPositionSecs (scroll)
{
    setOpaque (false);
    setTitle ("Tempo-Lane");   // benannt = ansprechbar (VoiceOver, Testlauf)
}

te::Edit* TempoLane::getEdit() const
{
    return appState.editManager.getEdit();
}

//==============================================================================
// Mapping
//==============================================================================

double TempoLane::xToTime (int x) const
{
    return juce::jmax (0.0, scrollPositionSecs + (double) x / pixelsPerSecond);
}

int TempoLane::timeToX (double sec) const
{
    return (int) ((sec - scrollPositionSecs) * pixelsPerSecond);
}

float TempoLane::bpmToY (double bpm) const
{
    // Logarithmisch: 40→240 BPM; doppeltes Tempo = gleicher Pixel-Abstand.
    const double norm = (std::log (juce::jlimit (kBpmMin, kBpmMax, bpm)) - std::log (kBpmMin))
                      / (std::log (kBpmMax) - std::log (kBpmMin));
    const float top = 3.0f, bottom = (float) getHeight() - 3.0f;
    return bottom - (float) norm * (bottom - top);
}

double TempoLane::yToBpm (float y) const
{
    const float top = 3.0f, bottom = (float) getHeight() - 3.0f;
    const double norm = juce::jlimit (0.0, 1.0, (double) (bottom - y) / (double) (bottom - top));
    return std::exp (std::log (kBpmMin) + norm * (std::log (kBpmMax) - std::log (kBpmMin)));
}

int TempoLane::tempoPointAtX (int x) const
{
    const auto points = tempoEngine.getTempoPoints();
    for (int i = points.size(); --i >= 0;)   // späterer Punkt gewinnt bei Überlappung
        if (std::abs (timeToX (points[i].timeSec) - x) <= 6)
            return i;
    return -1;
}

int TempoLane::timeSigBadgeAtX (int x) const
{
    const auto sigs = tempoEngine.getTimeSigPoints();
    for (int i = sigs.size(); --i >= 0;)
    {
        const int bx = timeToX (sigs[i].timeSec);
        if (x >= bx && x <= bx + 30)
            return i;
    }
    return -1;
}

//==============================================================================
// Paint
//==============================================================================

void TempoLane::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17171c));

    const auto points = tempoEngine.getTempoPoints();
    if (points.isEmpty())
        return;

    // Tempo-Stufenkurve (TE-Curve 0 = Sprung; Rampen zeichnen wir, sobald wir
    // Kurven-Editing anbieten — Punkte sind hier immer Steps).
    g.setColour (juce::Colour (0xff44aaff).withAlpha (0.75f));
    juce::Path path;
    for (int i = 0; i < points.size(); ++i)
    {
        const float x = (float) timeToX (points[i].timeSec);
        const float y = bpmToY (points[i].bpm);
        const float xNext = (i + 1 < points.size())
                                ? (float) timeToX (points[i + 1].timeSec)
                                : (float) getWidth();
        if (i == 0)
            path.startNewSubPath (juce::jmin (0.0f, x), y);
        path.lineTo (x, y);
        path.lineTo (xNext, y);
    }
    g.strokePath (path, juce::PathStrokeType (1.6f));

    // Punkte + BPM-Labels
    for (const auto& p : points)
    {
        const float x = (float) timeToX (p.timeSec);
        if (x < -20.0f || x > (float) getWidth() + 20.0f)
            continue;
        const float y = bpmToY (p.bpm);
        g.setColour (juce::Colour (0xff44aaff));
        g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (juce::String (p.bpm, p.bpm == std::floor (p.bpm) ? 0 : 1),
                    (int) x + 5, (int) y - 12, 44, 12, juce::Justification::centredLeft);
    }

    // Taktart-Badges (unten links am Wechselpunkt)
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (const auto& s : tempoEngine.getTimeSigPoints())
    {
        const int x = timeToX (s.timeSec);
        if (x < -40 || x > getWidth() + 40)
            continue;
        const auto text = juce::String (s.numerator) + "/" + juce::String (s.denominator);
        g.setColour (juce::Colour (0xff2a2a32));
        g.fillRoundedRectangle ((float) x + 1.0f, (float) getHeight() - 13.0f, 28.0f, 12.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (text, x + 1, getHeight() - 13, 28, 12, juce::Justification::centred);
    }
}

//==============================================================================
// Interaktion
//==============================================================================

void TempoLane::mouseDown (const juce::MouseEvent& e)
{
    const int pointIdx = tempoPointAtX (e.x);

    if (e.mods.isPopupMenu())
    {
        if (pointIdx >= 0)                          showTempoPointMenu (pointIdx);
        else if (int ts = timeSigBadgeAtX (e.x); ts >= 0)  showTimeSigMenu (ts);
        else                                        showEmptyAreaMenu (xToTime (e.x));
        return;
    }

    if (pointIdx >= 0)
    {
        const auto points = tempoEngine.getTempoPoints();
        dragIndex    = pointIdx;
        dragStartBpm = points[pointIdx].bpm;
        dragStartY   = e.position.y;
        dragTxnOpen  = false;   // Transaktion erst bei der ersten echten Bewegung
    }
}

void TempoLane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0)
        return;

    const float dy = e.position.y - dragStartY;
    if (! dragTxnOpen && std::abs (dy) < 2.0f)
        return;

    // Shift = Feinmodus (0.1 BPM/px), sonst log-Mapping der Lane.
    double newBpm;
    if (e.mods.isShiftDown())
        newBpm = dragStartBpm - (double) dy * 0.1;
    else
        newBpm = yToBpm (bpmToY (dragStartBpm) + dy);

    if (! e.mods.isShiftDown())
        newBpm = std::round (newBpm * 10.0) / 10.0;   // 0.1-BPM-Raster

    tempoEngine.setTempoBpm (dragIndex, newBpm, ! dragTxnOpen);
    dragTxnOpen = true;
    repaint();
}

void TempoLane::mouseUp (const juce::MouseEvent&)
{
    dragIndex   = -1;
    dragTxnOpen = false;
}

void TempoLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tempoPointAtX (e.x) >= 0)
        return;   // Doppelklick auf Punkt: nichts (Drag/Menü decken alles)

    tempoEngine.addTempoChange (xToTime (e.x));
    repaint();
}

juce::MouseCursor TempoLane::getMouseCursor()
{
    const auto pos = getMouseXYRelative();
    if (tempoPointAtX (pos.x) >= 0)
        return juce::MouseCursor::UpDownResizeCursor;
    return juce::MouseCursor::NormalCursor;
}

//==============================================================================
// Menüs
//==============================================================================

void TempoLane::showTempoPointMenu (int index)
{
    juce::PopupMenu m;
    m.addItem (1, juce::String::fromUTF8 ("BPM eingeben\xe2\x80\xa6"));
    m.addItem (2, juce::String::fromUTF8 ("Tempowechsel l\xc3\xb6schen"), index > 0);

    m.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<TempoLane> (this), index] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1) safe->promptBpm (index);
            if (r == 2) { safe->tempoEngine.removeTempoChange (index); safe->repaint(); }
        });
}

void TempoLane::showEmptyAreaMenu (double timeSec)
{
    juce::PopupMenu sig;
    const std::pair<int, int> sigs[] = { {4,4}, {3,4}, {6,8}, {2,4}, {5,4}, {7,8}, {12,8} };
    for (int i = 0; i < (int) std::size (sigs); ++i)
        sig.addItem (100 + i, juce::String (sigs[i].first) + "/" + juce::String (sigs[i].second));

    juce::PopupMenu m;
    m.addItem (1, juce::String::fromUTF8 ("Tempowechsel hier einf\xc3\xbcgen"));
    m.addSubMenu (juce::String::fromUTF8 ("Taktart hier \xc3\xa4ndern"), sig);

    m.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<TempoLane> (this), timeSec] (int r)
        {
            if (safe == nullptr) return;
            const std::pair<int, int> sigTable[] = { {4,4}, {3,4}, {6,8}, {2,4}, {5,4}, {7,8}, {12,8} };
            if (r == 1)
                safe->tempoEngine.addTempoChange (timeSec);
            else if (r >= 100 && r < 100 + (int) std::size (sigTable))
                safe->tempoEngine.addTimeSigChange (timeSec,
                                                    sigTable[r - 100].first,
                                                    sigTable[r - 100].second);
            safe->repaint();
        });
}

void TempoLane::showTimeSigMenu (int index)
{
    juce::PopupMenu m;
    const std::pair<int, int> sigs[] = { {4,4}, {3,4}, {6,8}, {2,4}, {5,4}, {7,8}, {12,8} };
    for (int i = 0; i < (int) std::size (sigs); ++i)
        m.addItem (100 + i, juce::String (sigs[i].first) + "/" + juce::String (sigs[i].second));
    m.addSeparator();
    m.addItem (1, juce::String::fromUTF8 ("Taktartwechsel l\xc3\xb6schen"), index > 0);

    m.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<TempoLane> (this), index] (int r)
        {
            if (safe == nullptr) return;
            const std::pair<int, int> sigTable[] = { {4,4}, {3,4}, {6,8}, {2,4}, {5,4}, {7,8}, {12,8} };
            if (r == 1)
                safe->tempoEngine.removeTimeSigChange (index);
            else if (r >= 100 && r < 100 + (int) std::size (sigTable))
                safe->tempoEngine.setTimeSig (index, sigTable[r - 100].first, sigTable[r - 100].second);
            safe->repaint();
        });
}

void TempoLane::promptBpm (int index)
{
    const auto points = tempoEngine.getTempoPoints();
    if (index < 0 || index >= points.size())
        return;

    auto* dialog = new juce::AlertWindow (juce::String::fromUTF8 ("Tempo \xc3\xa4ndern"),
                                          "BPM (20\xe2\x80\x93""400):",
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("bpm", juce::String (points[index].bpm, 1));
    dialog->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<TempoLane> (this), dialog, index] (int r)
            {
                if (safe != nullptr && r == 1)
                {
                    const double bpm = dialog->getTextEditorContents ("bpm").getDoubleValue();
                    if (bpm > 0.0)
                    {
                        safe->tempoEngine.setTempoBpm (index, bpm);
                        safe->repaint();
                    }
                }
                delete dialog;
            }),
        false);
}
