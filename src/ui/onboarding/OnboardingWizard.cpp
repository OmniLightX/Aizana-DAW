#include "OnboardingWizard.h"
#include "app/ApplicationState.h"

namespace
{
    const juce::Colour kBg            { 0xff141414 };
    const juce::Colour kCard          { 0xff222222 };
    const juce::Colour kBorder        { 0xff3a3a3a };
    const juce::Colour kText          { 0xffffffff };
    const juce::Colour kTextMuted     { 0xff999999 };
    const juce::Colour kPrimary       { 0xff4488ff };
    const juce::Colour kSeg           { 0xff2f2f2f };

    struct GenreDef { const char* name; juce::StringArray tracks; double bpm; const char* loudness; };

    const std::vector<GenreDef>& genres()
    {
        static const std::vector<GenreDef> g {
            { "Pop",        { "Gesang", "Drums", "Bass", "Keys" },        120.0, "-10 LUFS" },
            { "Hip-Hop",    { "Vocals", "Beat", "Bass", "FX" },           90.0,  "-9 LUFS"  },
            { "Rock",       { "Gesang", "Drums", "Bass", "Gitarre" },     128.0, "-8 LUFS"  },
            { "Electronic", { "Lead", "Drums", "Bass", "Pad" },           126.0, "-7 LUFS"  },
            { "Akustik",    { "Gesang", "Gitarre", "Percussion", "Bass" },100.0, "-12 LUFS" },
        };
        return g;
    }

    juce::Rectangle<int> cardRectFor (juce::Rectangle<int> bounds)
    {
        return juce::Rectangle<int> (0, 0, 600, 400).withCentre (bounds.getCentre());
    }
}

OnboardingWizard::OnboardingWizard (ApplicationState& appState_, std::function<void()> onFinished_)
    : appState (appState_), onFinished (std::move (onFinished_))
{
    for (int i = 0; i < (int) genres().size(); ++i)
    {
        auto* b = genreButtons.add (new juce::TextButton (genres()[(size_t) i].name));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId,   kSeg);
        b->setColour (juce::TextButton::buttonOnColourId, kPrimary);
        b->setColour (juce::TextButton::textColourOffId,  kTextMuted);
        b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        b->onClick = [this, i]
        {
            genreIndex = i;
            for (int j = 0; j < genreButtons.size(); ++j)
                genreButtons[j]->setToggleState (j == genreIndex, juce::dontSendNotification);

            // Genre only *suggests* a tempo — seed the slider, the user still decides.
            bpmSlider.setValue (genres()[(size_t) i].bpm, juce::dontSendNotification);
        };
        addChildComponent (b);
    }

    // Starter tempo — freely chosen on the Confirm step (seeded by the genre suggestion).
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setRange (40.0, 220.0, 1.0);
    bpmSlider.setValue (genres()[0].bpm, juce::dontSendNotification);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 26);
    bpmSlider.setColour (juce::Slider::trackColourId,         kPrimary);
    bpmSlider.setColour (juce::Slider::backgroundColourId,    kSeg);
    bpmSlider.setColour (juce::Slider::thumbColourId,         juce::Colours::white);
    bpmSlider.setColour (juce::Slider::textBoxTextColourId,   kText);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    bpmSlider.onValueChange = [this] { repaint(); };
    addChildComponent (bpmSlider);

    auto styleNav = [this] (juce::TextButton& b, juce::Colour col)
    {
        b.setColour (juce::TextButton::buttonColourId, col);
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        addChildComponent (b);
    };
    styleNav (backButton, kSeg);
    styleNav (nextButton, kPrimary);
    backButton.onClick = [this] { gotoStep (step - 1); };
    nextButton.onClick = [this] { if (step < 2) gotoStep (step + 1); else finish(); };

    gotoStep (0);
}

void OnboardingWizard::gotoStep (int newStep)
{
    step = juce::jlimit (0, 2, newStep);

    nextButton.setButtonText (step == 0 ? "Los geht's" : step == 1 ? "Weiter" : "Fertig - los!");
    backButton.setVisible (step > 0);
    nextButton.setVisible (true);

    for (auto* b : genreButtons)
        b->setVisible (step == 1);
    for (int j = 0; j < genreButtons.size(); ++j)
        genreButtons[j]->setToggleState (j == genreIndex, juce::dontSendNotification);

    bpmSlider.setVisible (step == 2);

    resized();
    repaint();
}

void OnboardingWizard::resized()
{
    auto card = cardRectFor (getLocalBounds()).reduced (32);

    // genre buttons row (step 1)
    if (! genreButtons.isEmpty())
    {
        const int n = genreButtons.size();
        const int gap = 8;
        const int totalGap = gap * (n - 1);
        const int bw = (card.getWidth() - totalGap) / n;
        const int by = card.getCentreY() - 18;
        for (int i = 0; i < n; ++i)
            genreButtons[i]->setBounds (card.getX() + i * (bw + gap), by, bw, 40);
    }

    // tempo slider (step 2) — sits on the "Tempo" row, right of the label.
    // `card` here equals paint()'s `in` rect, so the label and slider align.
    bpmSlider.setBounds (card.getX() + 78, card.getY() + 124, card.getWidth() - 78, 30);

    // nav buttons (bottom of card)
    const int bh = 34, bw = 120;
    nextButton.setBounds (card.getRight() - bw, card.getBottom() - bh, bw, bh);
    backButton.setBounds (card.getX(), card.getBottom() - bh, 96, bh);
}

void OnboardingWizard::paint (juce::Graphics& g)
{
    g.fillAll (kBg);   // opaque — first-run cover

    auto card = cardRectFor (getLocalBounds());
    g.setColour (kCard);
    g.fillRoundedRectangle (card.toFloat(), 14.0f);
    g.setColour (kBorder);
    g.drawRoundedRectangle (card.toFloat().reduced (0.75f), 14.0f, 1.0f);

    auto in = card.reduced (32);

    // step indicator dots
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == step ? kPrimary : kBorder);
        g.fillEllipse ((float) (in.getX() + i * 16), (float) (in.getY()), 8.0f, 8.0f);
    }

    g.setColour (kText);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    auto titleArea = juce::Rectangle<int> (in.getX(), in.getY() + 22, in.getWidth(), 36);

    g.setColour (kTextMuted);
    const auto subFont = juce::FontOptions (15.0f);
    auto subArea = juce::Rectangle<int> (in.getX(), in.getY() + 62, in.getWidth(), 60);

    if (step == 0)
    {
        g.setColour (kText); g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("Willkommen bei Cloudes", titleArea, juce::Justification::topLeft, false);
        g.setColour (kTextMuted); g.setFont (subFont);
        g.drawFittedText ("Sag, was du willst - Cloudes macht das echte Engineering und zeigt dir wie. "
                          "Lass uns deinen ersten Song aufsetzen.",
                          subArea, juce::Justification::topLeft, 3);
    }
    else if (step == 1)
    {
        g.setColour (kText); g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("Worum geht's?", titleArea, juce::Justification::topLeft, false);
        g.setColour (kTextMuted); g.setFont (subFont);
        g.drawText ("Waehle einen Stil - ich richte passende Spuren ein.",
                    subArea.withHeight (24), juce::Justification::topLeft, false);
    }
    else // step 2
    {
        const auto& gd = genres()[(size_t) genreIndex];
        g.setColour (kText); g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText ("Fertig zum Loslegen", titleArea, juce::Justification::topLeft, false);

        g.setColour (kTextMuted); g.setFont (subFont);
        g.drawText (juce::String ("Ich lege fuer ") + gd.name + " an:",
                    subArea.withHeight (24), juce::Justification::topLeft, false);

        g.setColour (kPrimary); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (gd.tracks.joinIntoString ("   |   "),
                    juce::Rectangle<int> (in.getX(), in.getY() + 96, in.getWidth(), 24),
                    juce::Justification::topLeft, false);

        // "Tempo" label — the slider beside it (positioned in resized) carries the value.
        g.setColour (kTextMuted); g.setFont (subFont);
        g.drawText ("Tempo",
                    juce::Rectangle<int> (in.getX(), in.getY() + 128, 74, 24),
                    juce::Justification::centredLeft, false);

        g.setColour (kTextMuted); g.setFont (subFont);
        g.drawText (juce::String ("Lautheits-Ziel ") + gd.loudness,
                    juce::Rectangle<int> (in.getX(), in.getY() + 166, in.getWidth(), 24),
                    juce::Justification::topLeft, false);
        g.drawFittedText ("Du landest in der Einsteiger-Welt - Cloudes hoert mit und schlaegt Schritte vor.",
                          juce::Rectangle<int> (in.getX(), in.getY() + 198, in.getWidth(), 48),
                          juce::Justification::topLeft, 2);
    }
}

void OnboardingWizard::buildStarterProject()
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    const auto& gd = genres()[(size_t) genreIndex];
    auto& um = appState.editManager.getUndoManager();
    um.beginNewTransaction ("Onboarding setup");

    auto existing = te::getAudioTracks (*edit);
    for (int i = 0; i < gd.tracks.size(); ++i)
    {
        te::AudioTrack* t = nullptr;
        if (i < existing.size())
            t = existing[i];
        else if (auto ptr = edit->insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (*edit), nullptr))
            t = ptr.get();

        if (t != nullptr)
        {
            t->setSoloIsolate (false);
            t->setName (gd.tracks[i]);
        }
    }

    // Tempo: the user's chosen BPM (slider), seeded by — but not locked to — the genre.
    const double chosenBpm = juce::jlimit (40.0, 220.0, bpmSlider.getValue());
    auto& seq = edit->tempoSequence;
    if (seq.getNumTempos() > 0)
        seq.getTempo (0)->setBpm (chosenBpm);
}

void OnboardingWizard::finish()
{
    buildStarterProject();
    appState.uiState.setMode (UIStateManager::UIMode::Beginner);
    appState.uiState.setAssistEnabled (true);
    appState.uiState.setOnboardingCompleted (true);
    if (onFinished)
        onFinished();
}
