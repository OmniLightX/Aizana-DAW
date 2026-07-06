#include <optional>
#include "DockedEditorPanel.h"

DockedEditorPanel::DockedEditorPanel (EditManager& em,
                                      std::function<te::Clip*()> provider,
                                      CompingEngine* comping)
    : editManager (em), selectionProvider (std::move (provider)), compingEngine (comping)
{
    emptyLabel.setText (juce::String::fromUTF8 ("W\xc3\xa4hle einen Audio- oder MIDI-Clip aus, "
                                                "um ihn hier zu bearbeiten."),
                        juce::dontSendNotification);
    emptyLabel.setJustificationType (juce::Justification::centred);
    emptyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
    emptyLabel.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (emptyLabel);
}

DockedEditorPanel::~DockedEditorPanel() = default;

void DockedEditorPanel::setDockActive (bool active)
{
    if (active)
    {
        startTimerHz (10);
        rebuildFor (selectionProvider ? selectionProvider() : nullptr);
    }
    else
    {
        stopTimer();
        clear();
    }
}

void DockedEditorPanel::clear()
{
    audioEditor.reset();
    pianoRoll.reset();
    currentClip = nullptr;
    keepAlive   = nullptr;
    emptyLabel.setVisible (true);
    resized();
    repaint();
}

void DockedEditorPanel::showClip (te::Clip* clip)
{
    rebuildFor (clip);
}

void DockedEditorPanel::rebuildFor (te::Clip* clip)
{
    if (clip == currentClip)
        return;

    // S1-Anker: beim Wechsel Audio-Clip → Audio-Clip übernimmt der neue Editor
    // Zeitachse + Werkzeug-Zustand des alten — die Edit-View springt nicht,
    // wenn nach einem Split/Cut das andere Teilstück selektiert wird.
    std::optional<AudioClipEditor::ViewHandover> handover;
    if (audioEditor != nullptr && dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
        handover = audioEditor->getViewHandover();

    audioEditor.reset();
    pianoRoll.reset();
    currentClip = clip;
    keepAlive   = clip;   // hold a reference for as long as an editor shows it

    if (auto* wave = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        audioEditor = std::make_unique<AudioClipEditor> (*wave, editManager, compingEngine);
        addAndMakeVisible (*audioEditor);
        emptyLabel.setVisible (false);
    }
    else if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
    {
        pianoRoll = std::make_unique<PianoRollView> (*midi, editManager);
        addAndMakeVisible (*pianoRoll);
        emptyLabel.setVisible (false);
    }
    else
    {
        currentClip = nullptr;   // unsupported clip type = empty state
        emptyLabel.setVisible (true);
    }

    resized();

    // Handover NACH resized() anwenden — das Lane-Mapping braucht gültige Bounds
    if (audioEditor != nullptr && handover.has_value())
        audioEditor->applyViewHandover (*handover);

    repaint();
}

void DockedEditorPanel::timerCallback()
{
    // Follow the global selection. The provider reads the live ClipComponent
    // selection, which the arrangement clears/re-resolves on deletes and
    // rebuilds — so a returned pointer is a currently-alive clip.
    rebuildFor (selectionProvider ? selectionProvider() : nullptr);
}

void DockedEditorPanel::resized()
{
    auto area = getLocalBounds();
    if (audioEditor != nullptr) audioEditor->setBounds (area);
    if (pianoRoll   != nullptr) pianoRoll->setBounds (area);
    emptyLabel.setBounds (area);
}

void DockedEditorPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c22));
}
