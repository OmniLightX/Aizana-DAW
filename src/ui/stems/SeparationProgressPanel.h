#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * SeparationProgressPanel — modeless status card for the stem separation job
 * (Phase 3). Same Apple-minimal pattern as ScanProgressPanel: bottom-right
 * overlay, paint-only, never blocks the transport or editing.
 *
 * Shows the model download and the (minutes-long) inference progress. Drive
 * from the message thread only — StemSeparationEngine marshals its callbacks.
 */
class SeparationProgressPanel : public juce::Component
{
public:
    SeparationProgressPanel()
    {
        setInterceptsMouseClicks (false, true);
        cancelButton.setComponentID ("cancel-stems");
        addAndMakeVisible (cancelButton);
        cancelButton.onClick = [this] { auto callback = onCancel; if (callback) callback(); };
    }

    std::function<void()> onCancel;
    void resized() override
    {
        cancelButton.setBounds (getWidth() - 103, 9, 90, 23);
    }

    /** Apply progress (0..1) + a human-readable stage line. Message thread only. */
    void update (float fraction01, const juce::String& stageText)
    {
        fraction = juce::jlimit (0.0f, 1.0f, fraction01);
        stage    = stageText;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (juce::Colour (0xee202327));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0x40ffffff));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);

        auto body = r.reduced (14.0f, 11.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("KI-Stem-Trennung — ")
                        + juce::String ((int) std::round (fraction * 100.0f)) + "%",
                    body.removeFromTop (18.0f).withTrimmedRight (94.0f), juce::Justification::centredLeft);

        g.setColour (juce::Colour (0xffb6bcc4));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (stage, body.removeFromTop (16.0f), juce::Justification::centredLeft);

        // Thin progress bar
        auto bar = body.removeFromBottom (5.0f);
        g.setColour (juce::Colour (0xff3a3f45));
        g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (juce::Colour (0xff4f8fd9));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (5.0f, bar.getWidth() * fraction)), 2.5f);
    }

private:
    juce::TextButton cancelButton { "Abbrechen" };
    float        fraction = 0.0f;
    juce::String stage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeparationProgressPanel)
};
