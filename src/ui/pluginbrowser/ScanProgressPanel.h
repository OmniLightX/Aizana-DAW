#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../plugins/PluginScanner.h"

/**
 * ScanProgressPanel — a small, modeless status card shown while plugins scan.
 *
 * Apple-minimal: non-blocking, no modal dialog. It lives as an overlay in the
 * bottom-right corner of the main window and updates live from the scanner's
 * progress callback (count + current plugin + a thin progress bar). It is shown
 * when a scan starts and hidden when it finishes.
 *
 * Pure paint() — no child components — so it is cheap to repaint on every probed
 * plugin and trivial to position. Drive it from the message thread only
 * (the scanner marshals its background callbacks via callAsync).
 */
class ScanProgressPanel : public juce::Component
{
public:
    ScanProgressPanel()
    {
        setInterceptsMouseClicks (false, false);   // overlay must not steal clicks
    }

    /** Apply a fresh progress snapshot. Call on the message thread. */
    void update (const PluginScanner::ScanProgress& p)
    {
        count    = p.numPluginsFound;
        fraction = juce::jlimit (0.0f, 1.0f, p.fraction);
        format   = p.formatName;
        current  = p.currentPlugin;
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

        // Title + live count
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("Scanning plugins  —  " + juce::String (count) + " found",
                    body.removeFromTop (18.0f), juce::Justification::centredLeft, true);

        // Current plugin / format (de-emphasised, ellipsised)
        body.removeFromTop (3.0f);
        g.setColour (juce::Colour (0xffb0b6bd));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        const auto line = current.isNotEmpty()
                            ? (format.isNotEmpty() ? "[" + format + "]  " : juce::String()) + current
                            : juce::String ("Preparing…");
        g.drawText (line, body.removeFromTop (15.0f), juce::Justification::centredLeft, true);

        // Thin progress bar
        body.removeFromTop (6.0f);
        auto bar = body.removeFromTop (4.0f);
        g.setColour (juce::Colour (0x33ffffff));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (juce::Colour (0xff3b9dff));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fraction), 2.0f);
    }

private:
    int          count    = 0;
    float        fraction = 0.0f;
    juce::String format;
    juce::String current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScanProgressPanel)
};
