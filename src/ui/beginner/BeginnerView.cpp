#include "BeginnerView.h"
#include "app/ApplicationState.h"
#include "engine/MixerEngine.h"

namespace
{
    // Cloudes palette (matches the real Pro view + sketch theme)
    const juce::Colour kBg            { 0xff1a1a1a };
    const juce::Colour kBarBg         { 0xff141414 };
    const juce::Colour kBorder        { 0xff333333 };
    const juce::Colour kSurfaceRaised { 0xff2a2a2a };
    const juce::Colour kBgDeep        { 0xff111111 };
    const juce::Colour kText          { 0xffffffff };
    const juce::Colour kTextSecondary { 0xffcccccc };
    const juce::Colour kTextMuted     { 0xff888888 };
    const juce::Colour kTextFaint     { 0xff666666 };
    const juce::Colour kSeg           { 0xff2a2a2a };
    const juce::Colour kPrimary       { 0xff4488ff };  // selection / primary
    const juce::Colour kGold          { 0xffddcc00 };  // Pro accent
    const juce::Colour kTeal          { 0xff1fa882 };  // assist / Cloudes voice
    const juce::Colour kTealInk       { 0xff06241c };
    const juce::Colour kGreen         { 0xff00cc44 };

    constexpr int kTopBar  = 56;
    constexpr int kLaneH   = 54;
    constexpr int kChildH  = 30;
    constexpr int kChildGap= 4;
    constexpr int kLaneGap = 8;
    constexpr int kColMaxW = 720;
    constexpr int kRailW   = 340;

    juce::Colour tintForName (const juce::String& n, int idx)
    {
        const auto s = n.toLowerCase();
        if (s.contains ("vocal") || s.contains ("gesang") || s.contains ("vox"))   return juce::Colour (0xff6aa0ff);
        if (s.contains ("drum")  || s.contains ("beat"))                            return juce::Colour (0xffff8a5c);
        if (s.contains ("bass"))                                                    return juce::Colour (0xffc08aff);
        if (s.contains ("key")   || s.contains ("synth") || s.contains ("melod")
            || s.contains ("piano") || s.contains ("lead"))                         return juce::Colour (0xff58c8a0);
        if (s.contains ("git")   || s.contains ("guitar"))                          return juce::Colour (0xffe0b85a);
        static const juce::Colour pal[] { juce::Colour (0xff6aa0ff), juce::Colour (0xffff8a5c),
                                          juce::Colour (0xffc08aff), juce::Colour (0xff58c8a0),
                                          juce::Colour (0xffe0b85a) };
        return pal[idx % 5];
    }

    void drawWave (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour c, int n, float scale)
    {
        g.setColour (c.withAlpha (0.5f));
        const float w = (float) area.getWidth() / (float) n;
        const float midY = (float) area.getCentreY();
        for (int i = 0; i < n; ++i)
        {
            const float h = 4.0f + std::abs (std::sin (i * 0.55f)) * scale;
            g.fillRoundedRectangle (area.getX() + i * w, midY - h * 0.5f, juce::jmax (1.0f, w - 2.0f), h, 1.0f);
        }
    }

}

//==============================================================================
// The Reveal overlay
//==============================================================================
BeginnerView::Reveal::Reveal()
{
    auto style = [this] (juce::TextButton& b, juce::Colour col, juce::Colour txt)
    {
        b.setColour (juce::TextButton::buttonColourId, col);
        b.setColour (juce::TextButton::textColourOffId, txt);
        addAndMakeVisible (b);
    };
    style (ok,      kSurfaceRaised, juce::Colours::white);
    style (showPro, kPrimary,       juce::Colours::white);
    ok.onClick      = [this] { setVisible (false); };
    showPro.onClick = [this] { setVisible (false); if (onShowPro) onShowPro(); };
}

void BeginnerView::Reveal::show (const juce::String& t, const juce::String& b)
{
    title = t; body = b;
    setVisible (true);
    toFront (false);
    resized();
    repaint();
}

void BeginnerView::Reveal::mouseDown (const juce::MouseEvent&)
{
    setVisible (false);   // click the dim backdrop to dismiss
}

void BeginnerView::Reveal::resized()
{
    auto card = juce::Rectangle<int> (0, 0, 560, 232).withCentre (getLocalBounds().getCentre());
    auto in   = card.reduced (22);
    showPro.setBounds (in.getRight() - 130, in.getBottom() - 30, 130, 30);
    ok.setBounds      (in.getRight() - 130 - 96, in.getBottom() - 30, 88, 30);
}

void BeginnerView::Reveal::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));

    auto card = juce::Rectangle<int> (0, 0, 560, 232).withCentre (getLocalBounds().getCentre());
    g.setColour (kSurfaceRaised);
    g.fillRoundedRectangle (card.toFloat(), 13.0f);
    g.setColour (kPrimary);
    g.drawRoundedRectangle (card.toFloat().reduced (0.75f), 13.0f, 1.5f);

    auto in = card.reduced (22);

    // OK badge + title
    g.setColour (kGreen);
    g.fillEllipse ((float) in.getX(), (float) in.getY(), 24.0f, 24.0f);
    g.setColour (kTealInk);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String::charToString ((juce::juce_wchar) 0x2713), in.getX(), in.getY(), 24, 24, juce::Justification::centred);
    g.setColour (kText);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (title, in.getX() + 34, in.getY(), in.getWidth() - 34, 26, juce::Justification::centredLeft, true);

    // "what I did" block
    auto bodyRect = juce::Rectangle<int> (in.getX(), in.getY() + 38, in.getWidth(), 96);
    g.setColour (kBgDeep);
    g.fillRoundedRectangle (bodyRect.toFloat(), 8.0f);
    g.setColour (kTextSecondary);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (body, bodyRect.reduced (12), juce::Justification::topLeft, 5);

    g.setColour (kTextFaint);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("Vorschau - die Engine kommt in Block B. Alles wird echte, editierbare Mechanik.",
                in.getX(), in.getBottom() - 26, in.getWidth() - 230, 16, juce::Justification::centredLeft, true);
}

//==============================================================================
BeginnerView::BeginnerView (ApplicationState& appState_, MixerEngine& mixerEngine_)
    : appState (appState_), mixerEngine (mixerEngine_)
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

    // Coach rail action buttons (static previews -> Block B wires the real IntentEngine).
    auto styleAct = [this] (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, kTeal);
        b.setColour (juce::TextButton::textColourOffId, kTealInk);
        addChildComponent (b);
    };
    styleAct (act1); styleAct (act2); styleAct (act3);
    // act button labels + onClick handlers are (re)bound per feedback item in refreshFeedback().

    cmdInput.setTextToShowWhenEmpty ("...oder sag's mir selbst", kTextMuted);
    cmdInput.setColour (juce::TextEditor::backgroundColourId, kBgDeep);
    cmdInput.setColour (juce::TextEditor::outlineColourId, kBorder);
    cmdInput.setColour (juce::TextEditor::textColourId, kText);
    cmdInput.onReturnKey = [this]
    {
        IntentEngine::Intent matched;
        if (matchKeyword (cmdInput.getText(), matched))
            runIntent (matched);   // heuristic keyword-lite; real NLP is Phase 3
        else
            showReveal ("Noch nicht verstanden",
                        "Probier einen der Vorschlaege oben - freie Texteingabe kommt mit Phase 3.");
        cmdInput.clear();
    };
    addChildComponent (cmdInput);

    addChildComponent (reveal);
    reveal.onShowPro = [this] { appState.uiState.setMode (UIStateManager::UIMode::Pro); };

    appState.uiState.addChangeListener (this);
    syncFromState();
    rebuildModel();
}

BeginnerView::~BeginnerView()
{
    appState.uiState.removeChangeListener (this);
}

//==============================================================================
bool BeginnerView::assistOn() const
{
    return appState.uiState.isAssistEnabled();
}

juce::Rectangle<int> BeginnerView::railArea() const
{
    if (! assistOn())
        return {};
    return { getWidth() - kRailW, kTopBar, kRailW, getHeight() - kTopBar };
}

juce::Rectangle<int> BeginnerView::railCardRect (int i) const
{
    auto rail = railArea().reduced (16);
    constexpr int headerH = 52, cardH = 96, gap = 10;
    return { rail.getX(), rail.getY() + headerH + i * (cardH + gap), rail.getWidth(), cardH };
}

void BeginnerView::showReveal (const juce::String& title, const juce::String& body)
{
    reveal.show (title, body);
}

void BeginnerView::refreshFeedback()
{
    if (! assistOn())
        return;

    auto fresh = appState.heuristicFeedbackEngine.current();

    juce::String sig;
    for (auto& f : fresh)
        sig << f.message << (f.hasIntent ? "*" : "") << '|';
    if (sig == lastFeedbackSig)
        return;                       // no change -> avoid needless rebind/repaint
    lastFeedbackSig = sig;
    feedback = std::move (fresh);

    juce::TextButton* acts[3] { &act1, &act2, &act3 };
    for (int i = 0; i < 3; ++i)
        if (i < (int) feedback.size() && feedback[i].hasIntent)
        {
            const auto intent = feedback[i].suggestedIntent;
            acts[i]->setButtonText (IntentEngine::label (intent));
            acts[i]->onClick = [this, intent] { runIntent (intent); };
        }

    resized();
    repaint();
}

void BeginnerView::runIntent (IntentEngine::Intent intent)
{
    // Target: none for now -> IntentEngine resolves to the first audio track;
    // loudness/glue intents ignore the target (they act on the master).
    auto plan = appState.intentEngine.plan (intent, nullptr);
    if (! plan.valid)
        return;

    // Real, editable mechanic — applied on the message thread as ONE undo step.
    appState.aiCommandBridge.enqueueBatch (plan.description, plan.commands);
    showReveal (plan.description, plan.mechanic);
}

bool BeginnerView::matchKeyword (const juce::String& text, IntentEngine::Intent& out)
{
    const auto s = text.toLowerCase();
    using I = IntentEngine::Intent;
    if (s.contains ("vorne") || s.contains ("gesang"))   { out = I::VocalForward;      return true; }
    if (s.contains ("leis"))                              { out = I::QuieterInMix;      return true; }
    if (s.contains ("mittig") || s.contains ("zentr"))    { out = I::CenterInMix;       return true; }
    if (s.contains ("warm"))                              { out = I::Warmer;            return true; }
    if (s.contains ("klar") || s.contains ("praesen"))    { out = I::Clearer;           return true; }
    if (s.contains ("punch") || s.contains ("druck"))     { out = I::MorePunch;         return true; }
    if (s.contains ("hall") || s.contains ("raum"))       { out = I::MoreReverb;        return true; }
    if (s.contains ("breit"))                             { out = I::Wider;             return true; }
    if (s.contains ("naeh") || s.contains ("nah"))        { out = I::Closer;            return true; }
    if (s.contains ("laut"))                              { out = I::Louder;            return true; }
    if (s.contains ("verkleb") || s.contains ("glue"))    { out = I::GlueMix;           return true; }
    if (s.contains ("pop") || s.contains ("referenz"))    { out = I::MatchPopReference; return true; }
    return false;
}

//==============================================================================
void BeginnerView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &appState.uiState)
    {
        syncFromState();
        resized();      // assist toggle changes the rail -> lane column reflows
        repaint();
    }
}

void BeginnerView::visibilityChanged()
{
    if (isVisible())
    {
        rebuildModel();
        lastFeedbackSig = {};        // force a feedback refresh on show
        refreshFeedback();
        resized();
        repaint();
        startTimer (1000);   // poll lanes + Coach feedback while visible
    }
    else
    {
        stopTimer();
    }
}

void BeginnerView::timerCallback()
{
    if (modelSignature() != lastSignature)
    {
        rebuildModel();
        resized();
        repaint();
    }
    refreshFeedback();   // live Coach feedback (no-op when assist off or unchanged)
}

void BeginnerView::syncFromState()
{
    const bool beginner = appState.uiState.getMode() == UIStateManager::UIMode::Beginner;
    beginnerSeg.setToggleState (beginner,   juce::dontSendNotification);
    proSeg.setToggleState      (! beginner, juce::dontSendNotification);
    assistToggle.setToggleState (appState.uiState.isAssistEnabled(), juce::dontSendNotification);
}

//==============================================================================
juce::String BeginnerView::modelSignature() const
{
    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return {};

    juce::String sig;
    for (auto* t : te::getAudioTracks (*edit))
    {
        sig << t->getName() << '|';
        if (MixerEngine::isFXChannel (*t)) sig << "fx";
        else if (MixerEngine::isBusChannel (*t)) sig << "bus";
        else if (auto* bus = mixerEngine.getBusChannelFor (*t)) sig << ">" << bus->getName();
        sig << ';';
    }
    return sig;
}

void BeginnerView::rebuildModel()
{
    lanes.clear();
    lastSignature = modelSignature();

    auto* edit = appState.editManager.getEdit();
    if (edit == nullptr)
        return;

    std::map<te::AudioTrack*, size_t> busLane;   // bus track -> index into lanes
    int colourIdx = 0;

    auto ensureBusLane = [&] (te::AudioTrack* bus) -> size_t
    {
        auto it = busLane.find (bus);
        if (it != busLane.end())
            return it->second;
        LaneVM vm;
        vm.name    = bus->getName();
        vm.isGroup = true;
        vm.tint    = tintForName (vm.name, colourIdx++);
        lanes.push_back (vm);
        const size_t idx = lanes.size() - 1;
        busLane[bus] = idx;
        return idx;
    };

    for (auto* t : te::getAudioTracks (*edit))
    {
        if (MixerEngine::isFXChannel (*t))      // effect returns are not "instruments" — hide
            continue;

        if (MixerEngine::isBusChannel (*t))     // a group header (children filled by routed tracks)
        {
            ensureBusLane (t);
            continue;
        }

        if (auto* bus = mixerEngine.getBusChannelFor (*t))   // routed into a bus -> child of that group
            lanes[ensureBusLane (bus)].children.push_back (t->getName());
        else                                                  // ungrouped -> atomic lane
        {
            LaneVM vm;
            vm.name = t->getName();
            vm.tint = tintForName (vm.name, colourIdx++);
            lanes.push_back (vm);
        }
    }
}

//==============================================================================
void BeginnerView::layoutLanes()
{
    const int avail = assistOn() ? getWidth() - kRailW : getWidth();
    const int colW  = juce::jmin (avail - 48, kColMaxW);
    const int x     = (avail - colW) / 2;
    int y           = kTopBar + 28;

    for (auto& l : lanes)
    {
        l.rowBounds = { x, y, colW, kLaneH };
        y += kLaneH;
        if (l.isGroup && l.expanded && ! l.children.empty())
            y += (int) l.children.size() * (kChildH + kChildGap) + kChildGap;
        y += kLaneGap;
    }
}

void BeginnerView::resized()
{
    // Dial (top-right) — always present.
    constexpr int pad = 16, gap = 8, h = 30, top = 13;
    int rx = getWidth() - pad;
    rx -= 90;       assistToggle.setBounds (rx, top, 90, h);
    rx -= gap + 64; proSeg.setBounds       (rx, top, 64, h);
    rx -= gap + 96; beginnerSeg.setBounds   (rx, top, 96, h);

    // Coach rail children — visible only when assist is on; one action button per
    // feedback item that carries a 1-click intent.
    const bool a = assistOn();
    cmdInput.setVisible (a);
    juce::TextButton* acts[3] { &act1, &act2, &act3 };
    for (int i = 0; i < 3; ++i)
    {
        const bool show = a && i < (int) feedback.size() && feedback[i].hasIntent;
        acts[i]->setVisible (show);
        if (show)
        {
            auto c = railCardRect (i);
            acts[i]->setBounds (c.getX() + 12, c.getBottom() - 34, 180, 26);
        }
    }
    if (a)
    {
        auto rail = railArea().reduced (16);
        cmdInput.setBounds (rail.getX(), rail.getBottom() - 34, rail.getWidth(), 30);
    }

    reveal.setBounds (getLocalBounds());

    layoutLanes();
}

void BeginnerView::mouseDown (const juce::MouseEvent& e)
{
    for (auto& l : lanes)
    {
        if (l.isGroup && ! l.children.empty() && l.rowBounds.contains (e.getPosition()))
        {
            l.expanded = ! l.expanded;
            layoutLanes();
            repaint();
            return;
        }
    }
}

//==============================================================================
void BeginnerView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // ── top bar ───────────────────────────────────────────────────────────────
    auto bar = getLocalBounds().removeFromTop (kTopBar);
    g.setColour (kBarBg);
    g.fillRect (bar);
    g.setColour (kBorder);
    g.drawHorizontalLine (kTopBar, 0.0f, (float) getWidth());

    g.setColour (kText);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Cloudes", bar.withTrimmedLeft (16).withWidth (110), juce::Justification::centredLeft, false);
    g.setColour (kTextMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Mein Song", bar.withTrimmedLeft (96).withWidth (200), juce::Justification::centredLeft, false);

    layoutLanes();

    // ── lanes (or empty state) ─────────────────────────────────────────────────
    if (lanes.empty())
    {
        const int avail = assistOn() ? getWidth() - kRailW : getWidth();
        g.setColour (kTextMuted);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Noch keine Spuren - oben rechts zu Pro wechseln und welche anlegen.",
                    juce::Rectangle<int> (0, kTopBar, avail, getHeight() - kTopBar),
                    juce::Justification::centred, false);
    }
    else
    {
        for (auto& l : lanes)
        {
            const auto r = l.rowBounds;
            g.setColour (kSurfaceRaised);
            g.fillRoundedRectangle (r.toFloat(), 9.0f);
            g.setColour (kBorder);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);

            const bool expandable = l.isGroup && ! l.children.empty();
            if (expandable)
            {
                g.setColour (kTextMuted);
                g.setFont (juce::FontOptions (12.0f));
                const auto caret = juce::String::charToString (l.expanded ? (juce::juce_wchar) 0x25BE
                                                                           : (juce::juce_wchar) 0x25B8);
                g.drawText (caret, r.getX() + 12, r.getY() + 17, 20, 20, juce::Justification::centred);
            }

            const int swatchX = r.getX() + (expandable ? 38 : 16);
            g.setColour (l.tint);
            g.fillRoundedRectangle ((float) swatchX, (float) (r.getY() + 10), 10.0f, 34.0f, 3.0f);

            const int nameX = swatchX + 22;
            g.setColour (kText);
            g.setFont (juce::FontOptions (15.0f, l.isGroup ? juce::Font::bold : juce::Font::plain));
            juce::String label = l.name;
            if (expandable) label << "   (" << (int) l.children.size() << ")";
            g.drawText (label, nameX, r.getY(), 200, r.getHeight(), juce::Justification::centredLeft, true);

            auto wave = juce::Rectangle<int> (nameX + 210, r.getY() + 12, r.getRight() - (nameX + 210) - 16, r.getHeight() - 24);
            if (wave.getWidth() > 40)
                drawWave (g, wave, l.tint, 44, 26.0f);

            if (l.isGroup && l.expanded)
            {
                int cy = r.getBottom() + kChildGap;
                for (auto& child : l.children)
                {
                    juce::Rectangle<int> cr (r.getX() + 28, cy, r.getWidth() - 28, kChildH);
                    g.setColour (kBgDeep);
                    g.fillRoundedRectangle (cr.toFloat(), 5.0f);
                    g.setColour (l.tint.withAlpha (0.7f));
                    g.fillRoundedRectangle ((float) (cr.getX() + 10), (float) (cr.getY() + 8), 7.0f, 14.0f, 3.0f);
                    g.setColour (kTextSecondary);
                    g.setFont (juce::FontOptions (13.0f));
                    g.drawText (child, cr.getX() + 28, cr.getY(), 200, cr.getHeight(), juce::Justification::centredLeft, true);
                    cy += kChildH + kChildGap;
                }
            }
        }
    }

    // ── Coach rail (assist on) ─────────────────────────────────────────────────
    if (assistOn())
    {
        auto rail = railArea();
        g.setColour (kBarBg);
        g.fillRect (rail);
        g.setColour (kBorder);
        g.drawVerticalLine (rail.getX(), (float) rail.getY(), (float) rail.getBottom());

        auto inner = rail.reduced (16);
        g.setColour (kTeal);
        g.fillEllipse ((float) inner.getX(), (float) inner.getY() + 2.0f, 30.0f, 30.0f);
        g.setColour (kTealInk);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("C", inner.getX(), inner.getY() + 2, 30, 30, juce::Justification::centred);
        g.setColour (kText);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("Cloudes", inner.getX() + 40, inner.getY(), 200, 18, juce::Justification::centredLeft);
        g.setColour (kTextMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("achtet auf deinen Mix", inner.getX() + 40, inner.getY() + 17, 220, 16, juce::Justification::centredLeft);

        const int n = juce::jmin (3, (int) feedback.size());
        for (int i = 0; i < n; ++i)
        {
            auto c = railCardRect (i);
            g.setColour (kSurfaceRaised);
            g.fillRoundedRectangle (c.toFloat(), 8.0f);
            g.setColour (feedback[i].hasIntent ? kTeal : kTextFaint);
            g.fillRect (c.getX(), c.getY() + 8, 3, c.getHeight() - 16);   // left accent
            g.setColour (kTextSecondary);
            g.setFont (juce::FontOptions (13.0f));
            const int textH = feedback[i].hasIntent ? 40 : (c.getHeight() - 20);
            g.drawFittedText (feedback[i].message, c.getX() + 14, c.getY() + 10, c.getWidth() - 24, textH,
                              juce::Justification::topLeft, 3);
        }
    }

    // gentle footer hint (only when there are lanes and no rail to crowd it)
    if (! lanes.empty())
    {
        g.setColour (kTextFaint);
        g.setFont (juce::FontOptions (13.0f));
        const int avail = assistOn() ? getWidth() - kRailW : getWidth();
        g.drawText ("Gruppen antippen zum Auf- und Zuklappen.",
                    juce::Rectangle<int> (0, getHeight() - 34, avail, 28),
                    juce::Justification::centred, false);
    }
}
