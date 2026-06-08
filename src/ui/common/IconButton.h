#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "CloudesIcons.h"

/**
 * IconButton — a general vector-icon button drawing a CloudesIcon (juce::Path).
 *
 * Replaces text-label transport/tool buttons with crisp, font-independent icons
 * (Sketch 005 Pro-UI polish). Colours are caller-configurable so the same button
 * serves the green Play, red Record, and neutral/toggle tool buttons. Toggle-aware:
 * when getToggleState() is true it uses the *On* colours.
 *
 * Pure paint — no engine deps. Keeps JUCE invariant I-04 in mind: drive toggle state
 * yourself in onClick (setClickingTogglesState(false)) for transport/tool semantics.
 */
class IconButton : public juce::Button
{
public:
    explicit IconButton (CloudesIcon icon) : juce::Button ({}), iconId (icon) {}

    void setIcon (CloudesIcon i) { iconId = i; repaint(); }

    // Icon (foreground) colours
    juce::Colour iconColour   { 0xffaaaaaa };
    juce::Colour iconColourOn { 0xffffffff };
    // Background fill (set alpha 0 / transparent for no background)
    juce::Colour bgColour     { 0xff3a3a3a };
    juce::Colour bgColourOn   { 0x00000000 };
    float        cornerRadius = 5.0f;
    float        iconScale    = 0.46f;   // icon size relative to the smaller dimension

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // background
        juce::Colour bg = (on && bgColourOn.getAlpha() > 0) ? bgColourOn : bgColour;
        if (bg.getAlpha() > 0)
        {
            if (down)      bg = bg.brighter (0.10f);
            else if (over) bg = bg.brighter (0.06f);
            g.setColour (bg);
            g.fillRoundedRectangle (bounds.reduced (1.0f), cornerRadius);
        }

        // icon
        juce::Colour fg = on ? iconColourOn : iconColour;
        if (! on && over) fg = fg.brighter (0.4f);
        g.setColour (fg);

        auto p = CloudesIcons::path (iconId);
        const float side = juce::jmin (bounds.getWidth(), bounds.getHeight()) * iconScale;
        auto target = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre());
        p.applyTransform (p.getTransformToScaleToFit (target, true));

        if (CloudesIcons::stroked (iconId))
            g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        else
            g.fillPath (p);
    }

private:
    CloudesIcon iconId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconButton)
};
