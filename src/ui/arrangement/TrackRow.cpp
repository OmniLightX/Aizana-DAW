#include "TrackRow.h"

//==============================================================================
TrackRow::TrackRow (te::AudioTrack&   track_,
                    AutomationEngine& automationEngine_,
                    double&           pixelsPerSecond_,
                    double&           scrollPositionSeconds_)
    : track               (track_)
    , automationEngine    (automationEngine_)
    , pixelsPerSecond     (pixelsPerSecond_)
    , scrollPositionSeconds (scrollPositionSeconds_)
{
    addAndMakeVisible (clipArea);
    buildDisclosureButton();
    addAndMakeVisible (disclosureButton);
}

//==============================================================================
void TrackRow::buildDisclosureButton()
{
    disclosureButton.setTooltip ("Toggle automation lane");
    disclosureButton.onClick = [this] { toggleAutomationLane(); };
    disclosureButton.setColour (juce::TextButton::buttonColourId,
                                juce::Colours::transparentBlack);
    disclosureButton.setColour (juce::TextButton::textColourOffId,
                                juce::Colours::grey);
}

//==============================================================================
void TrackRow::toggleAutomationLane()
{
    expanded = !expanded;

    if (expanded)
    {
        automationLane = std::make_unique<AutomationLane> (
            track, "volume", automationEngine,
            pixelsPerSecond, scrollPositionSeconds);
        addAndMakeVisible (*automationLane);
    }
    else
    {
        if (automationLane != nullptr)
            removeChildComponent (automationLane.get());
        automationLane.reset();
    }

    // Notify parent (ArrangementView) to re-layout all track rows
    if (auto* parent = getParentComponent())
        parent->resized();
}

//==============================================================================
int TrackRow::getPreferredHeight() const
{
    return expanded ? kExpandedHeight : kCollapsedHeight;
}

//==============================================================================
void TrackRow::resized()
{
    auto area = getLocalBounds();

    // Disclosure button: small square in top-right corner
    disclosureButton.setBounds (area.removeFromRight (20).removeFromTop (20));

    // Clip area: top portion (always kCollapsedHeight)
    clipArea.setBounds (area.removeFromTop (kCollapsedHeight));

    // Automation lane: remaining area when expanded
    if (expanded && automationLane != nullptr)
        automationLane->setBounds (area.removeFromTop (kAutomationLaneHeight));
}

//==============================================================================
void TrackRow::paint (juce::Graphics& g)
{
    // Separator line at the bottom of the row
    g.setColour (juce::Colours::darkgrey);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    // Subtle tint behind the automation lane when expanded
    if (expanded)
    {
        g.setColour (juce::Colour (0xFF1A1A2E).withAlpha (0.7f));
        g.fillRect (0, kCollapsedHeight, getWidth(), kAutomationLaneHeight);
    }
}
