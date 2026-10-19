#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

class BypassWrapper : public juce::Component
{
    // LED button: a 14×14 painted circle, green when active, red when bypassed.
    struct BypassLed : public juce::Button
    {
        te::Plugin& plugin;

        explicit BypassLed (te::Plugin& p)
            : juce::Button ("bypass")
            , plugin (p)
        {
            setTooltip ("Bypass (click to toggle)");
        }

        void paintButton (juce::Graphics& g, bool mouseOver, bool isDown) override
        {
            const bool active = plugin.isEnabled();
            const float alpha = isDown ? 0.55f : (mouseOver ? 1.0f : 0.80f);
            const auto  b     = getLocalBounds().toFloat().reduced (1.5f);

            // Dark ring
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (b);

            // LED fill — green = active, red = bypassed
            g.setColour ((active ? juce::Colour (0xff33dd33) : juce::Colour (0xffdd3333))
                            .withAlpha (alpha));
            g.fillEllipse (b.reduced (2.0f));

            // Specular highlight dot (top-left)
            const auto hi = b.reduced (b.getWidth() * 0.32f).translated (-1.0f, -1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.fillEllipse (hi);
        }
    };

public:
    static constexpr int kZoomBarH = 20;

    std::function<void(float)> onZoomChanged;

    BypassWrapper (te::Plugin& plugin_, juce::Component* editorComp_)
        : plugin (plugin_)
        , editorComp (editorComp_)
        , led (plugin_)
    {
        if (editorComp != nullptr)
            addAndMakeVisible (*editorComp);

        addAndMakeVisible (led);

        led.onClick = [this]
        {
            plugin.setEnabled (! plugin.isEnabled());
            led.repaint();
        };

        // Zoom buttons
        const struct { const char* label; float factor; } kZooms[] = {
            { "80%",  0.8f },
            { "100%", 1.0f },
            { "120%", 1.2f },
            { "150%", 1.5f },
        };

        for (auto& z : kZooms)
        {
            auto* btn = zoomButtons.add (new juce::TextButton (z.label));
            const float f = z.factor;
            btn->onClick = [this, f] { if (onZoomChanged) onZoomChanged (f); };
            btn->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff333333));
            btn->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffbbbbbb));
            btn->setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff555555));
            addAndMakeVisible (btn);
        }

        const int edW = editorComp ? editorComp->getWidth()  : 400;
        const int edH = editorComp ? editorComp->getHeight() : 300;
        setSize (edW, edH + kZoomBarH);
    }

    void resized() override
    {
        const int zoomBarY = getHeight() - kZoomBarH;

        if (editorComp != nullptr)
            editorComp->setBounds (0, 0, getWidth(), zoomBarY);

        // LED: 14×14, top-right corner, 5px from edges
        led.setBounds (getWidth() - 19, 5, 14, 14);

        // Zoom buttons: divide the zoom bar evenly
        const int n    = zoomButtons.size();
        const int btnW = getWidth() / n;
        for (int i = 0; i < n; ++i)
            zoomButtons[i]->setBounds (i * btnW, zoomBarY, btnW, kZoomBarH);
    }

    void paint (juce::Graphics& g) override
    {
        // Dark separator line above zoom bar
        const int zoomBarY = getHeight() - kZoomBarH;
        g.setColour (juce::Colour (0xff111111));
        g.drawHorizontalLine (zoomBarY, 0.0f, (float)getWidth());
        g.setColour (juce::Colour (0xff222222));
        g.fillRect (0, zoomBarY + 1, getWidth(), kZoomBarH - 1);
    }

private:
    te::Plugin&                    plugin;
    std::unique_ptr<juce::Component> editorComp;  // editor dies before the plugin
    BypassLed                      led;
    juce::OwnedArray<juce::TextButton> zoomButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BypassWrapper)
};
