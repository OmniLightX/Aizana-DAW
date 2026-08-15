#include "FloatingPluginWindow.h"
#include "PluginWindowManager.h"
#include "plugins/PluginLoadLog.h"
#include "ui/common/TransportKeys.h"

//==============================================================================
// BypassWrapper — overlays a small LED toggle button on the plugin editor and
// adds a 20px zoom bar at the bottom with 80/100/120/150% buttons.

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
    juce::Component*               editorComp;  // not owned
    BypassLed                      led;
    juce::OwnedArray<juce::TextButton> zoomButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BypassWrapper)
};

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
                             juce::DocumentWindow::closeButton
                             | juce::DocumentWindow::minimiseButton)
    , plugin  (plugin_)
    , manager (manager_)
{
    // CRITICAL: window creation must be on message thread (Pitfall 7)
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Use JUCE's own title bar — removes the native macOS green fullscreen
    // button so the window can never enter fullscreen accidentally.
    setUsingNativeTitleBar (false);
    setResizable (false, false);

    // Helper: create wrapper, store native size, wire zoom callback, set content.
    auto setupWrapper = [this] (juce::Component* editorComp)
    {
        auto* wrapper = new BypassWrapper (plugin, editorComp);

        // Store native editor dimensions (without the zoom bar).
        nativeEditorW = wrapper->getWidth();
        nativeEditorH = wrapper->getHeight() - BypassWrapper::kZoomBarH;
        currentZoom   = 1.0f;

        wrapper->onZoomChanged = [this] (float f) { applyZoom (f); };

        setContentOwned (wrapper, true);
        centreWithSize (wrapper->getWidth(),
                        wrapper->getHeight() + getTitleBarHeight());
    };

    // Stage 1: Try TE's Plugin::createEditor()
    AizanaPluginLog::stage ("FloatingPluginWindow  \"" + plugin.getName()
                             + "\"  -> plugin.createEditor()...");
    auto editorOwned = plugin.createEditor();
    AizanaPluginLog::stage ("FloatingPluginWindow  createEditor() returned "
                             + juce::String (editorOwned != nullptr ? "editor" : "null"));
    if (editorOwned != nullptr)
    {
        setupWrapper (editorOwned.release());
    }
    else
    {
        // Stage 2: Built-in plugins via BuiltInPluginFormat → AudioPluginInstance
        juce::AudioProcessorEditor* apiEditor = nullptr;

        if (auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (&plugin))
        {
            AizanaPluginLog::stage ("FloatingPluginWindow  externalPlugin->initialiseFully()...");
            // Force initialisation for plugins on tracks not yet in the render graph.
            // initialiseFully() is idempotent — the fullyInitialised flag prevents
            // double-init. For BuiltInPluginFormat this completes synchronously; for
            // async VST3/AU plugins the instance may still be null, in which case the
            // Stage 3 placeholder is the correct fallback.
            externalPlugin->initialiseFully();
            AizanaPluginLog::stage ("FloatingPluginWindow  initialiseFully() done");

            if (auto* pi = externalPlugin->getAudioPluginInstance())
            {
                AizanaPluginLog::stage ("FloatingPluginWindow  pi->createEditor()...");
                apiEditor = pi->createEditor();
                AizanaPluginLog::stage ("FloatingPluginWindow  pi->createEditor() done");

                // Stage 2b: plugins without a native editor (e.g. Aizana-generated
                // Faust/CLAP plugins, which expose params but no GUI) get a generic
                // editor built from getParameters() — real, labelled, working sliders.
                // Without this they fall through to the "No editor available" placeholder
                // and are only usable at their baked-in defaults.
                if (apiEditor == nullptr && pi->getParameters().size() > 0)
                {
                    AizanaPluginLog::stage ("FloatingPluginWindow  building GenericAudioProcessorEditor ("
                                             + juce::String (pi->getParameters().size()) + " params)");
                    apiEditor = new juce::GenericAudioProcessorEditor (*pi);
                }
            }
        }

        if (apiEditor != nullptr)
        {
            if (apiEditor->getWidth() == 0 || apiEditor->getHeight() == 0)
                apiEditor->setSize (400, 300);

            setupWrapper (apiEditor);
        }
        else
        {
            // Stage 3: No UI — show placeholder with bypass + zoom controls
            auto* placeholder = new juce::Label ("", "No editor available");
            placeholder->setSize (400, 200);
            placeholder->setJustificationType (juce::Justification::centred);
            placeholder->setColour (juce::Label::textColourId, juce::Colours::grey);
            setupWrapper (placeholder);
        }
    }

    // Add the pin button to the title bar area
    pinButton = std::make_unique<PinButton> (*this);
    pinButton->setSize (24, 24);

    // Make window visible
    setVisible (true);
    toFront (true);
    AizanaPluginLog::stage ("FloatingPluginWindow  \"" + plugin.getName() + "\"  READY");
}

FloatingPluginWindow::~FloatingPluginWindow()
{
    // Content component is owned by DocumentWindow, cleaned up automatically.
}

void FloatingPluginWindow::closeButtonPressed()
{
    manager.closePluginWindow (plugin);
}

bool FloatingPluginWindow::keyPressed (const juce::KeyPress& key)
{
    // Ein fokussiertes Textfeld hat Vorrang — sonst koennte man in einem Plugin
    // (Preset-Name, unser eigenes Frequenzfeld) kein Leerzeichen mehr tippen.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*> (focused) != nullptr)
            return juce::DocumentWindow::keyPressed (key);

    const auto action = TransportKeys::actionFor (key, juce::Time::currentTimeMillis(),
                                                  lastSpaceKeyTimeMs);
    if (action == TransportKeys::Action::none)
        return juce::DocumentWindow::keyPressed (key);

    auto& transport = plugin.edit.getTransport();

    if (action == TransportKeys::Action::rewindToStart)
    {
        transport.stop (false, false);
        transport.setPosition (tracktion::core::TimePosition::fromSeconds (0.0));
    }
    else if (transport.isPlaying())
    {
        transport.stop (false, false);
    }
    else
    {
        transport.play (false);
    }

    return true;
}

void FloatingPluginWindow::applyZoom (float factor)
{
    currentZoom = factor;

    const int newW = juce::roundToInt (nativeEditorW * factor);
    const int newH = juce::roundToInt (nativeEditorH * factor);

    // Resize the content (wrapper) to the zoomed editor size + zoom bar
    if (auto* wrapper = getContentComponent())
        wrapper->setSize (newW, newH + BypassWrapper::kZoomBarH);

    centreWithSize (newW, newH + BypassWrapper::kZoomBarH + getTitleBarHeight());
}
