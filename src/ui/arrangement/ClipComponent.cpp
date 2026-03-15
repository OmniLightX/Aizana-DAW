#include "ClipComponent.h"

//==============================================================================

ClipComponent::ClipComponent (te::Clip& clip_)
    : clip (clip_)
{
    setRepaintsOnMouseActivity (true);
}

void ClipComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Clip body — use a slightly desaturated version of the parent track colour
    juce::Colour fillColour = juce::Colour (0xff4a7a9b);  // default blue if no track colour
    if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
        fillColour = track->getColour().withSaturation (0.6f).withBrightness (0.5f);

    g.setColour (fillColour);
    g.fillRoundedRectangle (bounds.reduced (1.0f), 3.0f);

    // Highlight on top edge
    g.setColour (fillColour.brighter (0.4f));
    g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 1.5f,
                bounds.getRight() - 3.0f, bounds.getY() + 1.5f, 1.0f);

    // Border
    g.setColour (fillColour.darker (0.3f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 3.0f, 1.0f);

    // Clip name — single line, truncated if needed
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (clip.getName(),
                bounds.reduced (4.0f, 2.0f).toNearestInt(),
                juce::Justification::centredLeft,
                true);
}
