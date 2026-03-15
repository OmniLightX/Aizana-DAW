#include "MainWindow.h"
#include "MainComponent.h"

//==============================================================================

MainWindow::MainWindow (juce::String name)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    // Construct ApplicationState first — opens audio device and creates a new Edit.
    // Audio device failures are non-fatal (logged inside ApplicationState constructor).
    appState = std::make_unique<ApplicationState>();

    setUsingNativeTitleBar (true);

    // Replace placeholder with the real DAW shell (plan 01-08).
    // MainComponent takes ownership of the three-panel layout:
    //   TransportBar (top) + ArrangementView (middle) + MixerView (bottom).
    setContentOwned (new MainComponent (*appState), true);

    setResizable (true, true);
    centreWithSize (getWidth(), getHeight());

    setVisible (true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
