#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

/** Resize affordance at the top edge of the shared mixer/editor dock. */
class BottomDockSplitter : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int height = 7;

    BottomDockSplitter()
    {
        setName (juce::String::fromUTF8 ("Höhe des unteren Bereichs ändern"));
        setTitle (getName());
        setTooltip (juce::String::fromUTF8 ("Nach oben ziehen: Bereich vergrößern. Nach unten ziehen: verkleinern."));
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setMouseClickGrabsKeyboardFocus (false);
    }

    std::function<int()> getDockHeight;
    std::function<void(int)> onHeightChange;
    std::function<void()> onDragEnd;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1a1a1a));
        g.setColour (juce::Colour (0xff444444));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.setColour (juce::Colour (isMouseOverOrDragging() ? 0xff88bbee : 0xff888888));
        g.fillRoundedRectangle ((float) getWidth() * 0.5f - 16.0f,
                                (float) getHeight() * 0.5f - 1.0f, 32.0f, 2.0f, 1.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = e.mods.isLeftButtonDown() && getDockHeight != nullptr;
        moved = false;
        if (dragging)
        {
            startScreenY = e.getScreenY();
            startHeight = getDockHeight();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || ! onHeightChange)
            return;

        const int delta = e.getScreenY() - startScreenY;
        moved = moved || delta != 0;
        // Always use the original height, even while the parent clamps layout.
        // Reversing beyond a bound must not move the divider away from the pointer.
        onHeightChange (startHeight - delta);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const bool finishedResize = dragging && moved;
        dragging = false;
        moved = false;
        if (finishedResize && onDragEnd)
            onDragEnd();
        repaint();
    }

private:
    int startScreenY = 0;
    int startHeight = 0;
    bool dragging = false;
    bool moved = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomDockSplitter)
};
