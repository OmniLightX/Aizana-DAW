#include <catch2/catch_test_macros.hpp>
#include "ui/common/MixerControls.h"
#include "ui/common/PanKnobLookAndFeel.h"

TEST_CASE("Mixer controls: blank space beside fader selects parent rather than changing gain", "[mixer-ui]")
{
    juce::Component strip;
    strip.setSize(120, 400);
    strip.setVisible(true);
    aizana::MixerFader fader;
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
    fader.setBounds(0, 0, 92, 300);
    strip.addAndMakeVisible(fader);
    CHECK(strip.getComponentAt(80, 100) == &strip);
    CHECK(strip.getComponentAt(10, 100) == &strip);
    CHECK(strip.getComponentAt(46, 100) == &fader);
    auto* numericField = strip.getComponentAt(46, 292);
    REQUIRE(numericField != nullptr);
    CHECK(fader.isParentOf(numericField));
}

TEST_CASE("Mixer controls: pan responds only on its visible circular control", "[mixer-ui]")
{
    juce::Component strip;
    strip.setSize(120, 400);
    strip.setVisible(true);
    PanKnobLookAndFeel look;
    aizana::MixerPanKnob pan;
    pan.setLookAndFeel(&look);
    pan.setSliderStyle(juce::Slider::RotaryHorizontalDrag);
    pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pan.setBounds(0, 0, 120, 48);
    strip.addAndMakeVisible(pan);
    CHECK(strip.getComponentAt(5, 24) == &strip);
    CHECK(strip.getComponentAt(110, 24) == &strip);
    CHECK(strip.getComponentAt(40, 4) == &strip);
    CHECK(strip.getComponentAt(60, 24) == &pan);
    CHECK(strip.getComponentAt(60, 8) == &pan);
    pan.setLookAndFeel(nullptr);
}
