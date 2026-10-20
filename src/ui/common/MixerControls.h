#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace aizana
{
// Preserve layout/text entry, but let the channel receive clicks in unused space.
class MixerFader : public juce::Slider
{
public:
    bool hitTest(int x, int y) override
    {
        if (!getLocalBounds().contains(x, y)) return false;
        auto& look = getLookAndFeel();
        const auto layout = look.getSliderLayout(*this);
        if (getTextBoxPosition() != NoTextBox && layout.textBoxBounds.contains(x, y)) return true;
        const int halfWidth = juce::jmax(7, look.getSliderThumbRadius(*this) + 2);
        return std::abs(x - layout.sliderBounds.getCentreX()) <= halfWidth
            && y >= layout.sliderBounds.getY() - halfWidth
            && y < layout.sliderBounds.getBottom() + halfWidth;
    }
};

class MixerPanKnob : public juce::Slider
{
public:
    MixerPanKnob() { setSliderStyle(RotaryHorizontalDrag); }
    bool hitTest(int x, int y) override
    {
        const auto bounds = getLookAndFeel().getSliderLayout(*this).sliderBounds.toFloat().reduced(3.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        return getLocalBounds().contains(x, y)
            && juce::Point<float>(static_cast<float>(x), static_cast<float>(y))
                   .getDistanceFrom(bounds.getCentre()) <= radius;
    }
};
}
