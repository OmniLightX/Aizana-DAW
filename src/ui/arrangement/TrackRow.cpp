#include "TrackRow.h"
#include "ClipComponent.h"
#include "ArrangementView.h"
#include "ui/common/TrackColours.h"

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
    // clipArea itself does not intercept clicks (they pass through to TrackRow),
    // but its children (ClipComponents) still receive them.
    clipArea.setInterceptsMouseClicks (false, true);
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
    // Toggle state is managed entirely by toggleAutomationLane() via
    // disclosureButton.setToggleState(expanded, dontSendNotification).
    // Letting JUCE auto-toggle on click would create a race: JUCE flips
    // the button before onClick fires, then our code flips expanded again,
    // resulting in the lane immediately closing on first click.
}

//==============================================================================
void TrackRow::mouseDown (const juce::MouseEvent& e)
{
    // This fires only for clicks on EMPTY space (ClipComponents intercept clip clicks).
    if (auto* av = findParentComponentOfClass<ArrangementView>())
    {
        // Left-click on empty space: deselect clips directly, bypassing any
        // coordinate-check guard in ArrangementView::mouseDown.
        if (e.mods.isLeftButtonDown() && !e.mods.isPopupMenu())
            av->deselectAllClips();

        // Also forward the full event for playhead seek, right-click menu, etc.
        av->mouseDown (e.getEventRelativeTo (av));
    }
}

void TrackRow::mouseDrag (const juce::MouseEvent& e)
{
    // Forward drag to ArrangementView so rubber-band clip selection and Marquee
    // range drags work when dragging from empty track space.
    // TrackRow captures mouse after mouseDown, so without this forward AV::mouseDrag
    // would never fire for empty-space drags.
    if (auto* av = findParentComponentOfClass<ArrangementView>())
        av->mouseDrag (e.getEventRelativeTo (av));
}

void TrackRow::mouseUp (const juce::MouseEvent& e)
{
    // Forward mouseUp so ArrangementView can finalise rubber-band selection and
    // clear the band rect.
    if (auto* av = findParentComponentOfClass<ArrangementView>())
        av->mouseUp (e.getEventRelativeTo (av));
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
void TrackRow::setExpanded (bool shouldExpand)
{
    if (expanded == shouldExpand) return;
    toggleAutomationLane();  // reuse existing toggle logic
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

    // The actual clip area height depends on the total row height set by ArrangementView
    // (which applies trackHeightScale). The automation lane always stays at kAutomationLaneHeight;
    // the clip area takes everything above it so vertical zoom visibly expands clip blocks.
    const int totalH    = area.getHeight();
    const int clipAreaH = expanded ? juce::jmax (kCollapsedHeight, totalH - kAutomationLaneHeight)
                                   : totalH;

    // Disclosure button: 24x24px, bottom-left corner of the clip area zone
    // 2px inset from left and bottom edges — always visible regardless of horizontal scroll
    const int btnSize = kDisclosureButtonSize;
    disclosureButton.setBounds (2,
                                clipAreaH - btnSize - 2,
                                btnSize,
                                btnSize);

    // Clip area: grows with vertical zoom; full width
    clipArea.setBounds (0, 0, area.getWidth(), clipAreaH);

    // Automation lane: fixed height below the (now variable-height) clip area
    if (expanded && automationLane != nullptr)
        automationLane->setBounds (0, clipAreaH, area.getWidth(), kAutomationLaneHeight);
}

//==============================================================================
void TrackRow::updateScrollOffset()
{
    if (expanded && automationLane != nullptr)
        automationLane->updateScrollOffset();
}

void TrackRow::layoutClips (double pps)
{
    for (int i = 0; i < clipArea.getNumChildComponents(); ++i)
    {
        if (auto* cc = dynamic_cast<ClipComponent*> (clipArea.getChildComponent (i)))
        {
            const double startSecs  = cc->getClip().getPosition().getStart().inSeconds();
            const double lengthSecs = cc->getClip().getPosition().getLength().inSeconds();
            const int x = (int)(startSecs  * pps);
            const int w = juce::jmax (4, (int)(lengthSecs * pps));
            cc->setBounds (x, 0, w, clipArea.getHeight());
        }
    }
}

//==============================================================================
void TrackRow::paint (juce::Graphics& g)
{
    // Subtle track-colour wash behind the clip area so rows read as colour-coded
    // (Sketch 005 P1). The clipArea child is transparent, so this shows through
    // wherever a clip isn't drawn; clips themselves use the same colour.
    const int clipAreaH = expanded ? getHeight() - kAutomationLaneHeight : getHeight();
    g.setColour (AizanaTrackColours::displayColour (track).withAlpha (0.10f));
    g.fillRect (0, 0, getWidth(), clipAreaH);

    // Separator line at the bottom of the row
    g.setColour (juce::Colours::darkgrey);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));

    // Subtle tint behind the automation lane when expanded.
    // The automation lane sits below the clip area; use getHeight() - kAutomationLaneHeight
    // as the Y origin so the tint tracks vertical zoom correctly.
    if (expanded)
    {
        const int laneY = getHeight() - kAutomationLaneHeight;
        g.setColour (juce::Colour (0xFF1A1A2E).withAlpha (0.7f));
        g.fillRect (0, laneY, getWidth(), kAutomationLaneHeight);
    }
}
