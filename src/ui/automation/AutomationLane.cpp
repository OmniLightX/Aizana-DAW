#include "AutomationLane.h"

//==============================================================================
AutomationLane::AutomationLane (te::AudioTrack&    track_,
                                const juce::String& parameterID_,
                                AutomationEngine&   automationEngine_,
                                double&             pixelsPerSecond_,
                                double&             scrollPositionSeconds_)
    : track               (track_)
    , parameterID         (parameterID_)
    , automationEngine    (automationEngine_)
    , pixelsPerSecond     (pixelsPerSecond_)
    , scrollPositionSeconds (scrollPositionSeconds_)
{
    // Listen to the track's ValueTree so we repaint after undo / external changes
    track.state.addListener (this);

    // Benennung wie im uebrigen Arrangement (Spurname im Titel, damit bei mehreren
    // Spuren nichts mehrdeutig wird). Namenlose Komponenten sind weder fuer
    // VoiceOver noch fuer einen Testlauf ansprechbar — nur ueber Koordinaten, und
    // ein Koordinatenklick meldet auch dann Erfolg, wenn er danebengeht.
    setTitle              ("Automations-Lane "      + track.getName());
    paramSelector .setTitle ("Automations-Parameter " + track.getName());
    modeSelector  .setTitle ("Automations-Modus "     + track.getName());
    drawModeButton.setTitle ("Automation zeichnen "   + track.getName());

    // Parameter selector — T1.1: Volume/Pan + ALLE Plugin-Parameter der Spur
    // (qualifizierte plug:-IDs; siehe AutomationEngine::getAutomatableParams).
    rebuildParamChoices();
    paramSelector.onChange = [this]
    {
        const int idx = paramSelector.getSelectedId() - 1;   // IDs sind index+1
        if (idx >= 0 && idx < paramChoices.size())
            setParameterID (paramChoices.getReference (idx).id);
    };
    addAndMakeVisible (paramSelector);

    // Draw mode (Pencil) button — toggles always-create mode
    drawModeButton.setClickingTogglesState (false);   // manual toggle via setDrawMode
    drawModeButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
    drawModeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4488ff));
    drawModeButton.onClick = [this] { setDrawMode (!drawMode); };
    addAndMakeVisible (drawModeButton);

    // Automation mode selector — Read / Touch / Latch / Write
    modeSelector.addItem ("Read",  1);
    modeSelector.addItem ("Touch", 2);
    modeSelector.addItem ("Latch", 3);
    modeSelector.addItem ("Write", 4);

    // Sync to current engine mode so re-opening the lane reflects any prior changes
    {
        const AutomationMode currentMode = automationEngine.getMode (track);
        int modeId = 1; // read
        if      (currentMode == AutomationMode::touch) modeId = 2;
        else if (currentMode == AutomationMode::latch) modeId = 3;
        else if (currentMode == AutomationMode::write) modeId = 4;
        modeSelector.setSelectedId (modeId, juce::dontSendNotification);
    }

    modeSelector.onChange = [this]
    {
        switch (modeSelector.getSelectedId())
        {
            case 1: automationEngine.setMode (track, AutomationMode::read);  break;
            case 2: automationEngine.setMode (track, AutomationMode::touch); break;
            case 3: automationEngine.setMode (track, AutomationMode::latch); break;
            case 4: automationEngine.setMode (track, AutomationMode::write); break;
            default: break;
        }
    };
    addAndMakeVisible (modeSelector);

    // dB tooltip label — shown near cursor while dragging a volume automation point/segment
    dBLabel.setFont (juce::FontOptions (11.0f));
    dBLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    dBLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.7f));
    dBLabel.setJustificationType (juce::Justification::centred);
    dBLabel.setVisible (false);
    addChildComponent (dBLabel);

    setOpaque (false);
}

AutomationLane::~AutomationLane()
{
    track.state.removeListener (this);
}

void AutomationLane::rebuildParamChoices()
{
    paramChoices = automationEngine.getAutomatableParams (track);

    paramSelector.clear (juce::dontSendNotification);
    int selectedId = 1;   // default Volume
    for (int i = 0; i < paramChoices.size(); ++i)
    {
        paramSelector.addItem (paramChoices.getReference (i).displayName, i + 1);
        if (paramChoices.getReference (i).id == parameterID)
            selectedId = i + 1;
    }
    paramSelector.setSelectedId (selectedId, juce::dontSendNotification);
}

float AutomationLane::normFromNative (float native) const
{
    if (auto* p = getParam())
        return AutomationEngine::toNorm (*p, native);
    return juce::jlimit (0.0f, 1.0f, native);
}

float AutomationLane::nativeFromNorm (float norm) const
{
    if (auto* p = getParam())
        return AutomationEngine::fromNorm (*p, norm);
    return juce::jlimit (0.0f, 1.0f, norm);
}

//==============================================================================
// Parameter access helper
//==============================================================================

te::AutomatableParameter* AutomationLane::getParam() const
{
    // T1.1: qualifizierte plug:-IDs (und volume/pan) löst der Engine-Resolver —
    // die Stages unten bleiben als Fallback für Alt-Strings.
    if (auto* p = AutomationEngine::resolveParameter (track, parameterID))
        return p;

    if (parameterID.startsWith ("plug:"))
        return nullptr;   // qualifiziert & nicht gefunden = Plugin entfernt — kein Name-Fallback

    const auto& allParams = track.getAllAutomatableParams();

    // Stage 1: exact case-sensitive match (fastest, most reliable when paramID is correct)
    for (auto* p : allParams)
        if (p->paramID == parameterID)
            return p;

    // Stage 2: case-insensitive match (handles "Volume" vs "volume" discrepancy)
    for (auto* p : allParams)
        if (p->paramID.equalsIgnoreCase (parameterID))
            return p;

    // Stage 3: name-based match (handles TE versions where paramID diverges from our string)
    // "volume" matches params named "Volume", "Vol", etc.
    for (auto* p : allParams)
        if (p->paramName.containsIgnoreCase (parameterID))
            return p;

    // Debug: surface available params so developers can correct the paramID string
    // This fires in debug builds only — jassertfalse stops execution in debugger.
    DBG ("AutomationLane::getParam() -- no match for paramID='" + parameterID + "'. Available:");
    for (auto* p : allParams)
        DBG ("  paramID='" + p->paramID + "' name='" + p->paramName + "'");
    jassertfalse;  // Fires in debug builds — check the DBG output above for correct ID

    return nullptr;
}

//==============================================================================
// Paint
//==============================================================================

void AutomationLane::paint (juce::Graphics& g)
{
    // Dark semi-transparent background
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (getLocalBounds());

    // Parameter label — Anzeigename, nicht die (ggf. qualifizierte) interne ID
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (10.0f));
    juce::String labelText = parameterID;
    for (const auto& c : paramChoices)
        if (c.id == parameterID) { labelText = c.displayName; break; }
    g.drawText ("AUTO: " + labelText.toUpperCase(),
                4, 4, 160, 16, juce::Justification::centredLeft);

    // Zero / centre reference line (0.5 in normalised space = 0 dB or centre pan)
    float zeroY = valueToY (0.5f);
    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawHorizontalLine (static_cast<int> (zeroY), 0.0f, static_cast<float> (getWidth()));

    // Draw dB scale labels for volume parameter
    if (parameterID.equalsIgnoreCase ("volume"))
        drawDBScale (g);

    drawCurve  (g);
    drawPoints (g);
}

void AutomationLane::drawCurve (juce::Graphics& g) const
{
    auto* param = getParam();
    if (param == nullptr) return;

    auto& curve = param->getCurve();
    if (curve.getNumPoints() == 0) return;

    g.setColour (juce::Colours::orange.withAlpha (0.8f));
    juce::Path curvePath;
    bool pathStarted = false;

    // Sample the curve at each pixel column for a smooth-looking line
    for (int px = 0; px < getWidth(); ++px)
    {
        double t = xToTime (static_cast<float> (px));
        float  v = automationEngine.getValueAtTime (track, parameterID, t);
        float  y = valueToY (normFromNative (v));   // Kurve speichert NATIVE Werte

        if (!pathStarted)
        {
            curvePath.startNewSubPath (static_cast<float> (px), y);
            pathStarted = true;
        }
        else
        {
            curvePath.lineTo (static_cast<float> (px), y);
        }
    }

    g.strokePath (curvePath, juce::PathStrokeType (1.5f));
}

void AutomationLane::drawPoints (juce::Graphics& g) const
{
    auto* param = getParam();
    if (param == nullptr) return;

    auto& curve = param->getCurve();
    g.setColour (juce::Colours::orange);

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto pt = curve.getPoint (i);
        float x = timeToX (pt.time.inSeconds());
        float y = valueToY (normFromNative (pt.value));
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

void AutomationLane::drawDBScale (juce::Graphics& g) const
{
    // Standard dB reference values and their normalized fader positions
    const float dBValues[] = { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };

    for (auto dB : dBValues)
    {
        float faderPos = te::decibelsToVolumeFaderPosition (dB);
        float y = valueToY (faderPos);

        // Don't draw if outside visible bounds (with margin)
        if (y < 2.0f || y > static_cast<float> (getHeight()) - 2.0f)
            continue;

        // Faint horizontal reference line
        g.setColour (juce::Colours::grey.withAlpha (0.2f));
        g.drawHorizontalLine (static_cast<int> (y), 0.0f, static_cast<float> (getWidth()));
    }
}

//==============================================================================
// Mouse interaction
//==============================================================================

void AutomationLane::mouseDown (const juce::MouseEvent& e)
{
    // Draw mode: always create a new point, skip proximity check
    if (drawMode && e.mods.isLeftButtonDown() && !e.mods.isAltDown())
    {
        double timeSecs  = xToTime (e.position.x);
        float  normValue = juce::jlimit (0.0f, 1.0f, yToValue (e.position.y));
        automationEngine.addAutomationPoint (track, parameterID, timeSecs,
                                             nativeFromNorm (normValue));
        return;
    }

    // Right-click: delete the nearest point if within threshold
    if (e.mods.isRightButtonDown())
    {
        int idx = findPointNear (e.position);
        if (idx >= 0)
        {
            auto* param = getParam();
            if (param != nullptr)
            {
                double t = param->getCurve().getPoint (idx).time.inSeconds();
                automationEngine.removeAutomationPoint (track, parameterID, t);
            }
        }
        return;
    }

    // Cmd+click: segment drag — drag the line between two points up/down
    if (e.mods.isCommandDown())
    {
        int leftIdx = -1, rightIdx = -1;
        // Only enter segment mode if NOT on an existing point (point drag takes priority)
        if (findPointNear (e.position) < 0 && findSegmentAtPosition (e.position, leftIdx, rightIdx))
        {
            isSegmentDragging = true;
            segmentLeftIndex  = leftIdx;
            segmentRightIndex = rightIdx;
            segmentDragStartY = e.position.y;

            auto* param = getParam();
            if (param != nullptr)
            {
                segmentLeftStartValue  = param->getCurve().getPoint (leftIdx).value;
                segmentRightStartValue = param->getCurve().getPoint (rightIdx).value;

                // Begin a single undo transaction for the entire segment drag gesture.
                // movePoint calls in mouseDrag will NOT open new transactions,
                // so the whole drag folds into one undo step.
                param->getEdit().getUndoManager().beginNewTransaction ("Move automation segment");
            }
            return;
        }
    }

    // Alt+click: always create a new point, even if near an existing one
    if (e.mods.isAltDown())
    {
        double clickTime  = xToTime (e.position.x);
        float  clickValue = nativeFromNorm (yToValue (e.position.y));
        automationEngine.addAutomationPoint (track, parameterID, clickTime, clickValue);
        return;
    }

    // Left-click near existing point: start drag
    draggingPointIndex = findPointNear (e.position);
    if (draggingPointIndex >= 0)
    {
        auto* param = getParam();
        if (param != nullptr)
        {
            auto pt        = param->getCurve().getPoint (draggingPointIndex);
            dragStartTime  = pt.time.inSeconds();
            dragStartValue = pt.value;

            // Begin a single undo transaction for the entire drag gesture.
            // movePoint calls inside mouseDrag do NOT open new transactions,
            // so the whole drag folds into one undo step.
            param->getEdit().getUndoManager().beginNewTransaction ("Move automation point");
        }
    }
    else
    {
        // Left-click in empty area: add a new automation point
        double clickTime  = xToTime (e.position.x);
        float  clickValue = nativeFromNorm (yToValue (e.position.y));
        automationEngine.addAutomationPoint (track, parameterID, clickTime, clickValue);
    }
}

void AutomationLane::mouseDrag (const juce::MouseEvent& e)
{
    if (isSegmentDragging)
    {
        auto* param = getParam();
        if (param == nullptr) return;

        // Calculate value delta from Y movement
        float deltaY     = e.position.y - segmentDragStartY;
        float deltaValue = -(deltaY / static_cast<float> (getHeight())); // negative because Y is inverted

        // Move both bounding points by the same value delta
        auto& curve = param->getCurve();
        if (segmentLeftIndex  >= 0 && segmentLeftIndex  < curve.getNumPoints()
         && segmentRightIndex >= 0 && segmentRightIndex < curve.getNumPoints())
        {
            // Delta wirkt im NORM-Raum (pixelproportional), Werte bleiben nativ.
            float newLeftVal  = nativeFromNorm (normFromNative (segmentLeftStartValue)  + deltaValue);
            float newRightVal = nativeFromNorm (normFromNative (segmentRightStartValue) + deltaValue);

            // Use movePoint — no index shift, no remove+add cycle, no extra undo transactions.
            // The single transaction opened in mouseDown groups the entire drag into one undo step.
            auto ptL = curve.getPoint (segmentLeftIndex);
            auto ptR = curve.getPoint (segmentRightIndex);
            curve.movePoint (segmentLeftIndex,  ptL.time, newLeftVal,  false);
            curve.movePoint (segmentRightIndex, ptR.time, newRightVal, false);

            // Show dB tooltip for segment drag (average of left/right, volume param only)
            if (parameterID.equalsIgnoreCase ("volume"))
            {
                float avgVal = (newLeftVal + newRightVal) * 0.5f;
                float dB = te::volumeFaderPositionToDB (avgVal);
                dBLabel.setText (juce::String (dB, 1) + " dB", juce::dontSendNotification);
                dBLabel.setBounds (static_cast<int> (e.position.x) + 12,
                                   static_cast<int> (e.position.y) - 10,
                                   52, 18);
                dBLabel.setVisible (true);
            }
        }

        repaint();
        return;
    }

    if (draggingPointIndex < 0) return;

    auto* param = getParam();
    if (param == nullptr) return;

    auto& curve = param->getCurve();
    if (draggingPointIndex >= curve.getNumPoints()) return;

    double newTime  = juce::jmax (0.0, xToTime (e.position.x));
    float  newValue = juce::jlimit (0.0f, 1.0f, yToValue (e.position.y));

    // Move the point directly via the curve API — avoids remove+add cycle and the
    // undo transaction explosion it caused.  The single transaction opened in mouseDown
    // groups the entire drag into one undo step.
    curve.movePoint (draggingPointIndex,
                     tracktion::core::TimePosition::fromSeconds (newTime),
                     newValue,
                     false);

    dragStartTime  = newTime;
    dragStartValue = newValue;

    // Show dB tooltip while dragging a single point (volume parameter only)
    if (parameterID.equalsIgnoreCase ("volume"))
    {
        float dB = te::volumeFaderPositionToDB (newValue);
        dBLabel.setText (juce::String (dB, 1) + " dB", juce::dontSendNotification);
        dBLabel.setBounds (static_cast<int> (e.position.x) + 12,
                           static_cast<int> (e.position.y) - 10,
                           52, 18);
        dBLabel.setVisible (true);
    }

    repaint();
}

void AutomationLane::mouseUp (const juce::MouseEvent& /*e*/)
{
    dBLabel.setVisible (false);
    draggingPointIndex = -1;
    isSegmentDragging  = false;
    segmentLeftIndex   = -1;
    segmentRightIndex  = -1;
}

void AutomationLane::mouseMove (const juce::MouseEvent& e)
{
    // Show dB tooltip when hovering near a volume automation point
    if (!parameterID.equalsIgnoreCase ("volume"))
    {
        dBLabel.setVisible (false);
        return;
    }

    int idx = findPointNear (e.position);
    if (idx >= 0)
    {
        auto* param = getParam();
        if (param != nullptr)
        {
            auto pt  = param->getCurve().getPoint (idx);
            float x  = timeToX (pt.time.inSeconds());
            float y  = valueToY (pt.value);
            float dB = te::volumeFaderPositionToDB (pt.value);

            int labelX = juce::jmin (static_cast<int> (x) + 8, getWidth() - 50);
            int labelY = juce::jmax (0, static_cast<int> (y) - 20);

            dBLabel.setText (juce::String (dB, 1) + " dB", juce::dontSendNotification);
            dBLabel.setBounds (labelX, labelY, 52, 18);
            dBLabel.setVisible (true);
        }
    }
    else
    {
        dBLabel.setVisible (false);
    }
}

void AutomationLane::mouseDoubleClick (const juce::MouseEvent& /*e*/)
{
    // No special action for double-click in Phase 1
}

//==============================================================================
// Coordinate helpers
//==============================================================================

double AutomationLane::xToTime (float x) const
{
    // Arrangement (default): lane is full-width inside the scrolled
    // timelineContainer — local x=0 IS timeline 0s, no offset.
    // Viewport mode (Docked Editor): local x=0 = scrollPositionSeconds.
    return laneXToTime (x, pixelsPerSecond,
                        viewportTimeMapping ? scrollPositionSeconds : 0.0);
}

float AutomationLane::timeToX (double time) const
{
    return laneTimeToX (time, pixelsPerSecond,
                        viewportTimeMapping ? scrollPositionSeconds : 0.0);
}

float AutomationLane::valueToY (float value) const
{
    // value 0 = bottom, value 1 = top
    return static_cast<float> (getHeight()) * (1.0f - juce::jlimit (0.0f, 1.0f, value));
}

float AutomationLane::yToValue (float y) const
{
    return 1.0f - y / static_cast<float> (getHeight());
}

//==============================================================================
// Find nearest point
//==============================================================================

int AutomationLane::findPointNear (juce::Point<float> pos, float thresh) const
{
    auto* param = getParam();
    if (param == nullptr) return -1;

    auto& curve = param->getCurve();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto pt = curve.getPoint (i);
        float px = timeToX (pt.time.inSeconds());
        float py = valueToY (pt.value);
        if (pos.getDistanceFrom ({ px, py }) < thresh)
            return i;
    }
    return -1;
}

//==============================================================================
// Find segment between two adjacent points
//==============================================================================

bool AutomationLane::findSegmentAtPosition (juce::Point<float> pos, int& leftIdx, int& rightIdx, float thresh) const
{
    auto* param = getParam();
    if (param == nullptr) return false;

    auto& curve = param->getCurve();
    if (curve.getNumPoints() < 2) return false;

    // Check each consecutive pair of points
    for (int i = 0; i < curve.getNumPoints() - 1; ++i)
    {
        auto ptL = curve.getPoint (i);
        auto ptR = curve.getPoint (i + 1);

        float x1 = timeToX (ptL.time.inSeconds());
        float y1 = valueToY (ptL.value);
        float x2 = timeToX (ptR.time.inSeconds());
        float y2 = valueToY (ptR.value);

        // Check if pos.x is between x1 and x2
        if (pos.x < x1 || pos.x > x2) continue;

        // Distance from point to line segment
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = std::sqrt (dx * dx + dy * dy);
        if (len < 1.0f) continue;

        float dist = std::abs (dy * pos.x - dx * pos.y + x2 * y1 - y2 * x1) / len;
        if (dist < thresh)
        {
            leftIdx  = i;
            rightIdx = i + 1;
            return true;
        }
    }
    return false;
}

//==============================================================================
// Resized
//==============================================================================

void AutomationLane::resized()
{
    // Arrangement: full-width lane in the scrolled container — controls must be
    // offset by the scroll position to stay visible. Viewport mode (Docked
    // Editor): the lane itself is viewport-sized, controls sit at x=0.
    const int scrollX = viewportTimeMapping
                            ? 0
                            : static_cast<int> (scrollPositionSeconds * pixelsPerSecond);
    drawModeButton.setBounds (scrollX + 4,   2, 20, 18);
    paramSelector.setBounds  (scrollX + 28,  2, 80, 20);
    modeSelector.setBounds   (scrollX + 112, 2, 70, 20);
}

void AutomationLane::updateScrollOffset()
{
    // Called by TrackRow/ArrangementView whenever scroll or zoom changes.
    resized();
    repaint();
}

void AutomationLane::setViewportTimeMapping (bool useViewportMapping)
{
    viewportTimeMapping = useViewportMapping;
    resized();
    repaint();
}

//==============================================================================
// setParameterID
//==============================================================================

void AutomationLane::setParameterID (const juce::String& newParamID)
{
    parameterID = newParamID;
    repaint();
}

//==============================================================================
// setDrawMode
//==============================================================================

void AutomationLane::setDrawMode (bool drawModeActive)
{
    drawMode = drawModeActive;
    drawModeButton.setToggleState (drawModeActive, juce::dontSendNotification);
    setMouseCursor (drawModeActive
        ? juce::MouseCursor::CrosshairCursor
        : juce::MouseCursor::NormalCursor);
    repaint();
}

//==============================================================================
// ValueTree::Listener — repaint when automation data changes
//==============================================================================

void AutomationLane::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    repaint();
}

void AutomationLane::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    repaint();
}

void AutomationLane::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    repaint();
}
