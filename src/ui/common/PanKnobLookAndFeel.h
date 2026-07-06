#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * PanKnobLookAndFeel (UX4) — Studio-One-artiger Pan-Knob:
 * dunkler Grundring + heller Wert-Bogen, der von 12 Uhr (Mitte) zum aktuellen
 * Pan läuft, plus Zeiger. Der Bogen macht die Auslenkung auf einen Blick
 * ablesbar — der alte Default-Rotary zeigte praktisch nichts.
 */
class PanKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto  centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float lineW  = juce::jmax (2.5f, size * 0.09f);
        const float arcR   = radius - lineW * 0.5f;

        const float angle    = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const float midAngle = rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle); // 12 Uhr = C

        // Grundring (voller Drehbereich, dunkel)
        juce::Path back;
        back.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (juce::Colour (0xff3a3a42));
        g.strokePath (back, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Wert-Bogen: von der Mitte (C) zum aktuellen Wert — links/rechts symmetrisch
        if (std::abs (angle - midAngle) > 0.01f)
        {
            juce::Path val;
            val.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                               juce::jmin (midAngle, angle), juce::jmax (midAngle, angle), true);
            g.setColour (juce::Colour (0xff44aaff));
            g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Mitten-Tick (12 Uhr) als Referenz
        {
            const auto p1 = centre.getPointOnCircumference (arcR - lineW, midAngle);
            const auto p2 = centre.getPointOnCircumference (arcR + lineW * 0.5f, midAngle);
            g.setColour (juce::Colour (0xff777788));
            g.drawLine ({ p1, p2 }, 1.5f);
        }

        // Knopf-Fläche + Zeiger
        const float knobR = arcR - lineW * 1.2f;
        g.setColour (juce::Colour (0xff26262c));
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour (juce::Colour (0xff111114));
        g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        const auto tip  = centre.getPointOnCircumference (knobR - 2.0f, angle);
        const auto tail = centre.getPointOnCircumference (knobR * 0.35f, angle);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine ({ tail, tip }, 2.0f);
    }
};
