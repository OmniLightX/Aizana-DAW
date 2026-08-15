#include "MusicalTypingWindow.h"
#include "LiveMidiPreview.h"

//==============================================================================
class MusicalTypingWindow::Content : public juce::Component,
                                     private juce::MidiKeyboardState::Listener
{
public:
    explicit Content (std::function<te::Track*()> provider)
        : trackProvider (std::move (provider))
    {
        keyboard.setKeyPressBaseOctave (4);          // A-Reihe beginnt bei C4
        keyboard.setLowestVisibleKey (48);
        keyboard.setAvailableRange (24, 108);
        addAndMakeVisible (keyboard);

        hint.setText (juce::String::fromUTF8 (
                          "A W S E D F T G Y H U J  =  C bis H   ·   Pfeil links/rechts = Oktave   ·   "
                          "spielt auf der ausgewaehlten Spur"),
                      juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::centred);
        hint.setColour (juce::Label::textColourId, juce::Colours::grey);
        hint.setFont (juce::Font (11.0f));
        addAndMakeVisible (hint);

        state.addListener (this);
        setSize (620, 130);
        setWantsKeyboardFocus (true);
    }

    ~Content() override
    {
        state.removeListener (this);
        aizana::sendLivePreviewAllNotesOff (trackProvider != nullptr ? trackProvider() : nullptr);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        hint.setBounds (r.removeFromBottom (18));
        keyboard.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e2126)); }

    void visibilityChanged() override
    {
        // Ohne Tastaturfokus kommt kein Tastendruck an — und das Fenster saehe
        // aus wie kaputt (die Klaviatur reagiert auf die Maus, aber nicht aufs
        // Tippen). Deshalb den Fokus aktiv holen, sobald es sichtbar wird.
        if (isShowing())
            keyboard.grabKeyboardFocus();
    }

private:
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override
    {
        aizana::sendLivePreviewNoteOn (trackProvider != nullptr ? trackProvider() : nullptr,
                                       note, (int) (vel * 127.0f));
    }

    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override
    {
        aizana::sendLivePreviewNoteOff (trackProvider != nullptr ? trackProvider() : nullptr, note);
    }

    std::function<te::Track*()> trackProvider;
    juce::MidiKeyboardState state;
    juce::MidiKeyboardComponent keyboard { state, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::Label hint;
};

//==============================================================================
MusicalTypingWindow::MusicalTypingWindow (std::function<te::Track*()> trackProvider)
    : juce::DocumentWindow (juce::String::fromUTF8 ("Tastatur spielen"),
                            juce::Colour (0xff1e2126),
                            juce::DocumentWindow::closeButton)
{
    content = std::make_unique<Content> (std::move (trackProvider));
    setUsingNativeTitleBar (true);
    setContentNonOwned (content.get(), true);
    setResizable (true, false);
    setAlwaysOnTop (true);      // sonst verschwindet es hinter dem Hauptfenster
    centreWithSize (620, 130);
    setVisible (true);
}

MusicalTypingWindow::~MusicalTypingWindow()
{
    clearContentComponent();
    content.reset();
}

void MusicalTypingWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}
