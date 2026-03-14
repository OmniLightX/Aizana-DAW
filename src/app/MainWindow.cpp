#include "MainWindow.h"

//==============================================================================
/**
 * Placeholder content component — a blank gray surface.
 *
 * This will be replaced in plan 01-08 (UI Shell) with the real
 * ArrangementView + MixerView + TransportBar layout.
 */
class PlaceholderContent : public juce::Component
{
public:
    PlaceholderContent()
    {
        setSize (1200, 800);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff2a2a2a));   // dark gray — DAW neutral background

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText ("Cloudes — DAW Foundation (skeleton)",
                    getLocalBounds(),
                    juce::Justification::centred,
                    true);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaceholderContent)
};

//==============================================================================

MainWindow::MainWindow (juce::String name)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new PlaceholderContent(), true);

    setResizable (true, true);
    centreWithSize (getWidth(), getHeight());

    setVisible (true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
