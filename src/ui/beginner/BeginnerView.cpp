#include "BeginnerView.h"
#include "app/ApplicationState.h"

namespace
{
    // Cloudes palette (matches the real Pro view + sketch theme)
    const juce::Colour kBg        { 0xff1a1a1a };
    const juce::Colour kBarBg     { 0xff141414 };
    const juce::Colour kBorder    { 0xff333333 };
    const juce::Colour kText      { 0xffffffff };
    const juce::Colour kTextMuted { 0xff888888 };
    const juce::Colour kTextFaint { 0xff666666 };
    const juce::Colour kSeg       { 0xff2a2a2a };
    const juce::Colour kPrimary   { 0xff4488ff };  // selection / primary
    const juce::Colour kGold      { 0xffddcc00 };  // Pro accent
    const juce::Colour kTeal      { 0xff1fa882 };  // assist / Cloudes voice
}

BeginnerView::BeginnerView (ApplicationState& appState_)
    : appState (appState_)
{
    auto styleSeg = [this] (juce::TextButton& b, juce::Colour onColour)
    {
        b.setClickingTogglesState (false);                  // invariant I-04: drive state ourselves
        b.setColour (juce::TextButton::buttonColourId,   kSeg);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.setColour (juce::TextButton::textColourOffId,  kTextMuted);
        b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        addAndMakeVisible (b);
    };
    styleSeg (beginnerSeg,  kPrimary);
    styleSeg (proSeg,       kGold);
    styleSeg (assistToggle, kTeal);

    beginnerSeg.onClick  = [this] { appState.uiState.setMode (UIStateManager::UIMode::Beginner); };
    proSeg.onClick       = [this] { appState.uiState.setMode (UIStateManager::UIMode::Pro); };
    assistToggle.onClick = [this] { appState.uiState.toggleAssist(); };

    appState.uiState.addChangeListener (this);
    syncFromState();
}

BeginnerView::~BeginnerView()
{
    appState.uiState.removeChangeListener (this);
}

void BeginnerView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &appState.uiState)
    {
        syncFromState();
        repaint();
    }
}

void BeginnerView::syncFromState()
{
    const bool beginner = appState.uiState.getMode() == UIStateManager::UIMode::Beginner;
    beginnerSeg.setToggleState (beginner,   juce::dontSendNotification);
    proSeg.setToggleState      (! beginner, juce::dontSendNotification);
    assistToggle.setToggleState (appState.uiState.isAssistEnabled(), juce::dontSendNotification);
}

void BeginnerView::resized()
{
    constexpr int pad = 16, gap = 8, h = 30, top = 13;
    int x = getWidth() - pad;
    x -= 90;       assistToggle.setBounds (x, top, 90, h);
    x -= gap + 64; proSeg.setBounds       (x, top, 64, h);
    x -= gap + 96; beginnerSeg.setBounds   (x, top, 96, h);
}

void BeginnerView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // ── top bar ───────────────────────────────────────────────────────────────
    auto bar = getLocalBounds().removeFromTop (56);
    g.setColour (kBarBg);
    g.fillRect (bar);
    g.setColour (kBorder);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());

    g.setColour (kText);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Cloudes", bar.withTrimmedLeft (16).withWidth (110),
                juce::Justification::centredLeft, false);
    g.setColour (kTextMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Mein Song", bar.withTrimmedLeft (96).withWidth (200),
                juce::Justification::centredLeft, false);

    // ── centred placeholder (Slice 1 shell) ───────────────────────────────────
    auto body = getLocalBounds().withTrimmedTop (56);

    g.setColour (kText);
    g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
    g.drawText ("Einsteiger-Welt",
                body.withHeight (60).translated (0, body.getHeight() / 2 - 70),
                juce::Justification::centred, false);

    g.setColour (kTextMuted);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("Dieselbe Session wie Pro \xe2\x80\x94 oben rechts umschalten. "
                "Gruppierte Spuren & Coach folgen.",
                body.withHeight (24).translated (0, body.getHeight() / 2 - 6),
                juce::Justification::centred, false);

    g.setColour (kTextFaint);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Gesang  \xc2\xb7  Drums  \xc2\xb7  Melodie  \xc2\xb7  Bass",
                body.withHeight (24).translated (0, body.getHeight() / 2 + 36),
                juce::Justification::centred, false);
}
