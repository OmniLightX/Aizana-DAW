#include "FloatingPluginWindow.h"
#include "PluginWindowManager.h"

//==============================================================================
// FloatingPluginWindow::PinButton

FloatingPluginWindow::PinButton::PinButton (FloatingPluginWindow& ownerWindow)
    : juce::Button ("Pin")
    , owner (ownerWindow)
{
    setTooltip ("Pin to side panel");
}

void FloatingPluginWindow::PinButton::paintButton (juce::Graphics& g,
                                                    bool mouseOver,
                                                    bool isDown)
{
    const auto alpha = isDown ? 0.6f : (mouseOver ? 0.85f : 0.7f);
    g.setColour (juce::Colours::white.withAlpha (alpha));

    // Draw a simple "pin" icon: a filled circle with a vertical line
    const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.35f;

    // Circle (pin head)
    g.fillEllipse (cx - r, cy - bounds.getHeight() * 0.3f, r * 2.0f, r * 2.0f);

    // Vertical line (pin shaft)
    g.fillRect (cx - 1.0f, cy, 2.0f, bounds.getHeight() * 0.4f);
}

void FloatingPluginWindow::PinButton::clicked()
{
    owner.manager.pinPluginToSidePanel (owner.plugin);
}

//==============================================================================
// FloatingPluginWindow

FloatingPluginWindow::FloatingPluginWindow (te::Plugin& plugin_,
                                            PluginWindowManager& manager_)
    : juce::DocumentWindow (plugin_.getName(),
                             juce::Colour (0xff2a2a2a),
                             juce::DocumentWindow::allButtons)
    , plugin  (plugin_)
    , manager (manager_)
{
    // CRITICAL: window creation must be on message thread (Pitfall 7)
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    setUsingNativeTitleBar (true);
    setResizable (true, true);

    // Create and own the plugin's editor
    auto* editor = plugin.createEditorIfNeeded();
    if (editor != nullptr)
    {
        setContentOwned (editor, true);
        centreWithSize (editor->getWidth(),
                        editor->getHeight() + getTitleBarHeight());
    }
    else
    {
        // Fallback: create a placeholder component if plugin has no UI
        auto* placeholder = new juce::Label ("", "No editor available");
        placeholder->setSize (400, 200);
        placeholder->setJustificationType (juce::Justification::centred);
        placeholder->setColour (juce::Label::textColourId, juce::Colours::grey);
        setContentOwned (placeholder, true);
        centreWithSize (400, 200 + getTitleBarHeight());
    }

    // Add the pin button to the title bar area
    // Note: DocumentWindow with native title bar can't easily add custom buttons.
    // We add a "Pin" text button below the title bar instead, as an overlay.
    // This is the portable approach across macOS/Windows.
    pinButton = std::make_unique<PinButton> (*this);
    pinButton->setSize (24, 24);

    // Make window visible
    setVisible (true);
    toFront (true);
}

FloatingPluginWindow::~FloatingPluginWindow()
{
    // Content component is owned by DocumentWindow, cleaned up automatically.
}

void FloatingPluginWindow::closeButtonPressed()
{
    manager.closePluginWindow (plugin);
}
