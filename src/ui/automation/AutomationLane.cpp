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

    // Parameter selector dropdown (Volume / Pan for Phase 1)
    paramSelector.addItem ("Volume", 1);
    paramSelector.addItem ("Pan",    2);
    paramSelector.setSelectedId (1, juce::dontSendNotification);
    paramSelector.onChange = [this]
    {
        switch (paramSelector.getSelectedId())
        {
            case 1:  setParameterID ("volume"); break;
            case 2:  setParameterID ("pan");    break;
            default: break;
        }
    };
    addAndMakeVisible (paramSelector);

    setOpaque (false);
}

AutomationLane::~AutomationLane()
{
    track.state.removeListener (this);
}

//==============================================================================
// Parameter access helper
//==============================================================================

te::AutomatableParameter* AutomationLane::getParam() const
{
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

    // Parameter label
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("AUTO: " + parameterID.toUpperCase(),
                4, 4, 80, 16, juce::Justification::centredLeft);

    // Zero / centre reference line (0.5 in normalised space = 0 dB or centre pan)
    float zeroY = valueToY (0.5f);
    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawHorizontalLine (static_cast<int> (zeroY), 0.0f, static_cast<float> (getWidth()));

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
        float  y = valueToY (v);

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
        float y = valueToY (pt.value);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

//==============================================================================
// Mouse interaction
//==============================================================================

void AutomationLane::mouseDown (const juce::MouseEvent& e)
{
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
        }
    }
    else
    {
        // Left-click in empty area: add a new automation point
        double clickTime  = xToTime (e.position.x);
        float  clickValue = yToValue (e.position.y);
        automationEngine.addAutomationPoint (track, parameterID, clickTime, clickValue);
    }
}

void AutomationLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPointIndex < 0) return;

    // Remove old point position, re-add at new position
    automationEngine.removeAutomationPoint (track, parameterID, dragStartTime);

    double newTime  = juce::jmax (0.0, xToTime (e.position.x));
    float  newValue = juce::jlimit (0.0f, 1.0f, yToValue (e.position.y));
    automationEngine.addAutomationPoint (track, parameterID, newTime, newValue);

    // Update drag state so next mouseDrag removes the point we just added
    dragStartTime  = newTime;
    dragStartValue = newValue;

    repaint();
}

void AutomationLane::mouseUp (const juce::MouseEvent& /*e*/)
{
    draggingPointIndex = -1;
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
    return scrollPositionSeconds + static_cast<double> (x) / pixelsPerSecond;
}

float AutomationLane::timeToX (double time) const
{
    return static_cast<float> ((time - scrollPositionSeconds) * pixelsPerSecond);
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
// Resized
//==============================================================================

void AutomationLane::resized()
{
    // Parameter selector in top-right corner
    paramSelector.setBounds (getWidth() - 84, 2, 80, 20);
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
