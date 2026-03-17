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
    disclosureButton.setTooltip ("Show/hide automation lane");
    disclosureButton.onClick = [this] { toggleAutomationLane(); };

    // Collapsed state: subtle dark background with grey text — visible but not dominant
    disclosureButton.setColour (juce::TextButton::buttonColourId,
                                juce::Colour (0xff2a2a2a));
    disclosureButton.setColour (juce::TextButton::textColourOffId,
                                juce::Colour (0xff888888));
    // Expanded state: Apple system orange tint signals active state
    disclosureButton.setColour (juce::TextButton::buttonOnColourId,
                                juce::Colour (0xff3a2a10));  // very dark orange background
    disclosureButton.setColour (juce::TextButton::textColourOnId,
                                juce::Colour (0xffff9500));  // Apple system orange text
    disclosureButton.setClickingTogglesState (true);
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

    disclosureButton.setToggleState (expanded, juce::dontSendNotification);

    // Fire callback so ArrangementView can re-layout both headers and timeline
    if (onExpansionChanged)
        onExpansionChanged();
    else if (auto* parent = getParentComponent())
        parent->resized();  // fallback if callback not wired
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

    // Disclosure button: 24x24px, bottom-right corner of the clip area zone
    // 2px inset from right and bottom edges — accessible, non-dominant
    const int btnSize = kDisclosureButtonSize;
    disclosureButton.setBounds (area.getWidth() - btnSize - 2,
                                kCollapsedHeight - btnSize - 2,
                                btnSize,
                                btnSize);

    // Clip area: top kCollapsedHeight, full width (disclosure button overlaps via z-order)
    clipArea.setBounds (0, 0, area.getWidth(), kCollapsedHeight);

    // Automation lane: below the clip area when expanded
    if (expanded && automationLane != nullptr)
        automationLane->setBounds (0, kCollapsedHeight, area.getWidth(), kAutomationLaneHeight);
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
