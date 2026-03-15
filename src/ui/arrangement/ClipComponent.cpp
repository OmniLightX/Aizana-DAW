#include "ClipComponent.h"
#include "ui/pianoroll/PianoRollView.h"

//==============================================================================

ClipComponent::ClipComponent (te::Clip& clip_, EditManager& editManager_)
    : clip (clip_), editManager (editManager_)
{
    setRepaintsOnMouseActivity (true);
}

void ClipComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Clip body — use a slightly desaturated version of the parent track colour
    juce::Colour fillColour = juce::Colour (0xff4a7a9b);  // default blue if no track colour
    if (auto* track = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
        fillColour = track->getColour().withSaturation (0.6f).withBrightness (0.5f);

    // MIDI clips get a slightly different tint so they are visually distinct
    if (dynamic_cast<te::MidiClip*> (&clip) != nullptr)
        fillColour = fillColour.withHue (fillColour.getHue() + 0.1f);

    g.setColour (fillColour);
    g.fillRoundedRectangle (bounds.reduced (1.0f), 3.0f);

    // Highlight on top edge
    g.setColour (fillColour.brighter (0.4f));
    g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 1.5f,
                bounds.getRight() - 3.0f, bounds.getY() + 1.5f, 1.0f);

    // Border
    g.setColour (fillColour.darker (0.3f));
    g.drawRoundedRectangle (bounds.reduced (1.0f), 3.0f, 1.0f);

    // Clip name — single line, truncated if needed
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (clip.getName(),
                bounds.reduced (4.0f, 2.0f).toNearestInt(),
                juce::Justification::centredLeft,
                true);
}

void ClipComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    // Double-click on a MIDI clip → open Piano Roll editor in floating window
    if (auto* midiClip = dynamic_cast<te::MidiClip*> (&clip))
    {
        // Self-deleting DocumentWindow subclass that closes on X button press
        struct PianoRollWindow : public juce::DocumentWindow
        {
            PianoRollWindow (const juce::String& name, juce::Colour bg)
                : juce::DocumentWindow (name, bg, juce::DocumentWindow::closeButton)
            {}
            void closeButtonPressed() override { delete this; }
        };

        auto* editor = new PianoRollWindow (
            "Piano Roll - " + clip.getName(),
            juce::Colour (0xff2a2a2a));

        editor->setContentOwned (new PianoRollView (*midiClip, editManager), true);
        editor->setSize (900, 500);
        editor->setResizable (true, false);
        editor->setUsingNativeTitleBar (true);
        editor->setVisible (true);
    }
    // Audio clips: no action in Phase 1 (future: waveform editor)
}
