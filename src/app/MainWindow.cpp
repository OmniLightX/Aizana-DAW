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
    auto* mainComp = new MainComponent (*appState);
    setContentOwned (mainComp, true);

    // -------------------------------------------------------------------------
    // Menu bar wiring (plan 01-13)
    // -------------------------------------------------------------------------
#if JUCE_MAC
    // macOS: use the native system menu bar (appears at screen top, not in window).
    // The model is the MainComponent itself (it now implements MenuBarModel).
    juce::MenuBarModel::setMacMainMenu (mainComp);
#else
    // Windows / Linux: in-window menu bar rendered below the title bar.
    menuBarComponent = std::make_unique<juce::MenuBarComponent> (mainComp);
    // MenuBarComponent is added to the window directly so it sits above the
    // content component. resized() keeps it pinned to the top.
    addAndMakeVisible (*menuBarComponent);
#endif

    setResizable (true, true);
    centreWithSize (getWidth(), getHeight());

    setVisible (true);
}

MainWindow::~MainWindow()
{
#if JUCE_MAC
    // MUST clear the Mac menu bar model before destruction to prevent a
    // dangling pointer to the already-destroyed MainComponent.
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif

    // Tear down the content component (MainComponent → MixerView → MasterStrip,
    // ChannelStrips, TrackHeaders — every UI element that listens to the Edit)
    // BEFORE appState is destroyed. appState owns the Edit; member destruction
    // runs in this destructor's BODY-then-members order, but the content component
    // is owned by the base ResizableWindow and would otherwise be destroyed AFTER
    // appState (in ~ResizableWindow). That order frees the Edit first, then the UI
    // destructors call edit.getMaster…().state.removeListener() on a dangling Edit
    // → EXC_BAD_ACCESS (e.g. MasterStrip::~MasterStrip). Clearing content here, while
    // the destructor body runs (members still alive), keeps the Edit valid during UI teardown.
    clearContentComponent();
}

void MainWindow::resized()
{
    juce::DocumentWindow::resized();

#if ! JUCE_MAC
    // Keep the in-window menu bar pinned to the top of the client area.
    if (menuBarComponent != nullptr)
    {
        const int menuH = juce::LookAndFeel::getDefaultLookAndFeel()
                              .getDefaultMenuBarHeight();
        menuBarComponent->setBounds (0, 0, getWidth(), menuH);

        // Push MainComponent down so it does not overlap the menu bar.
        if (auto* content = getContentComponent())
            content->setBounds (0, menuH, getWidth(), getHeight() - menuH);
    }
#endif
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
