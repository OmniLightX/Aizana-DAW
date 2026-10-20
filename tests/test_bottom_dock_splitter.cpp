#include <catch2/catch_test_macros.hpp>
#include "ui/common/BottomDockSplitter.h"

namespace
{
juce::MouseEvent pointerEvent (BottomDockSplitter& splitter, int screenY,
                                bool dragged = false, bool left = true)
{
    const auto local = splitter.getLocalPoint (nullptr, juce::Point<int> (20, screenY)).toFloat();
    return { juce::Desktop::getInstance().getMainMouseSource(), local,
             juce::ModifierKeys (left ? juce::ModifierKeys::leftButtonModifier
                                     : juce::ModifierKeys::rightButtonModifier),
             1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &splitter, &splitter,
             juce::Time::getCurrentTime(), local, juce::Time::getCurrentTime(), 1, dragged };
}
}

TEST_CASE ("Bottom dock splitter follows the pointer through bounds and relayout", "[ui][dock-splitter]")
{
    BottomDockSplitter splitter;
    int height = 240;
    int saved = 0;
    splitter.setBounds (0, 600 - height, 800, BottomDockSplitter::height);
    splitter.getDockHeight = [&] { return height; };
    splitter.onHeightChange = [&] (int requested)
    {
        height = juce::jlimit (120, 400, requested);
        splitter.setTopLeftPosition (0, 600 - height);
    };
    splitter.onDragEnd = [&] { ++saved; };
    splitter.mouseDown (pointerEvent (splitter, 360));
    splitter.mouseDrag (pointerEvent (splitter, 300, true));
    CHECK (height == 300);
    splitter.mouseDrag (pointerEvent (splitter, 400, true));
    CHECK (height == 200);
    splitter.mouseDrag (pointerEvent (splitter, 600, true));
    CHECK (height == 120);
    splitter.mouseDrag (pointerEvent (splitter, 550, true));
    CHECK (height == 120); // Still beyond the lower bound: do not jump on reversal.
    splitter.mouseDrag (pointerEvent (splitter, 470, true));
    CHECK (height == 130);
    splitter.mouseDrag (pointerEvent (splitter, 100, true));
    CHECK (height == 400);
    splitter.mouseDrag (pointerEvent (splitter, 150, true));
    CHECK (height == 400);
    splitter.mouseDrag (pointerEvent (splitter, 210, true));
    CHECK (height == 390);
    CHECK (saved == 0);
    splitter.mouseUp (pointerEvent (splitter, 210, true));
    CHECK (saved == 1);
    splitter.mouseDrag (pointerEvent (splitter, 300, true));
    CHECK (height == 390); // No resize after release.
}

TEST_CASE ("Bottom dock splitter click and secondary drag do not resize or save", "[ui][dock-splitter]")
{
    BottomDockSplitter splitter;
    int updates = 0, saves = 0;
    splitter.getDockHeight = [] { return 240; };
    splitter.onHeightChange = [&] (int) { ++updates; };
    splitter.onDragEnd = [&] { ++saves; };
    splitter.mouseDown (pointerEvent (splitter, 360));
    splitter.mouseUp (pointerEvent (splitter, 360));
    splitter.mouseDown (pointerEvent (splitter, 360, false, false));
    splitter.mouseDrag (pointerEvent (splitter, 200, true, false));
    splitter.mouseUp (pointerEvent (splitter, 200, true, false));
    CHECK (updates == 0);
    CHECK (saves == 0);
    CHECK (splitter.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);
    CHECK (splitter.getTitle().isNotEmpty());
    CHECK (splitter.getTooltip().isNotEmpty());
}
