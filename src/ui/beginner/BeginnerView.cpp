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

    // Layout
    constexpr int kTopBar   = 56;
    constexpr int kLaneH    = 54;
    constexpr int kChildH   = 30;
    constexpr int kChildGap = 4;
    constexpr int kLaneGap  = 8;
    constexpr int kColMaxW  = 760;

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

    appState.uiState.addChangeListener (this);
    syncFromState();
    rebuildModel();
}

BeginnerView::~BeginnerView()
{
    appState.uiState.removeChangeListener (this);
}

//==============================================================================
void BeginnerView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &appState.uiState)
    {
        syncFromState();
        repaint();
    }
}

void BeginnerView::visibilityChanged()
{
    if (isVisible())
    {
        rebuildModel();
        resized();
        repaint();
        startTimer (1000);   // safety poll while visible — heavy edits happen in Pro
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
    const int colW = juce::jmin (getWidth() - 48, kColMaxW);
    const int x    = (getWidth() - colW) / 2;
    int y          = kTopBar + 28;   // below top bar + a little breathing room

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
    constexpr int pad = 16, gap = 8, h = 30, top = 13;
    int rx = getWidth() - pad;
    rx -= 90;       assistToggle.setBounds (rx, top, 90, h);
    rx -= gap + 64; proSeg.setBounds       (rx, top, 64, h);
    rx -= gap + 96; beginnerSeg.setBounds   (rx, top, 96, h);

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
    g.drawText ("Cloudes", bar.withTrimmedLeft (16).withWidth (110),
                juce::Justification::centredLeft, false);
    g.setColour (kTextMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Mein Song", bar.withTrimmedLeft (96).withWidth (200),
                juce::Justification::centredLeft, false);

    // ── empty state ─────────────────────────────────────────────────────────--
    if (lanes.empty())
    {
        g.setColour (kTextMuted);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Noch keine Spuren \xe2\x80\x94 oben rechts zu Pro wechseln und welche anlegen.",
                    getLocalBounds().withTrimmedTop (kTopBar),
                    juce::Justification::centred, false);
        return;
    }

    layoutLanes();

    for (auto& l : lanes)
    {
        const auto r = l.rowBounds;
        g.setColour (kSurfaceRaised);
        g.fillRoundedRectangle (r.toFloat(), 9.0f);
        g.setColour (kBorder);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);

        const bool expandable = l.isGroup && ! l.children.empty();

        // caret (groups only)
        if (expandable)
        {
            g.setColour (kTextMuted);
            g.setFont (juce::FontOptions (12.0f));
            const auto caret = juce::String::charToString (l.expanded ? (juce::juce_wchar) 0x25BE
                                                                       : (juce::juce_wchar) 0x25B8);
            g.drawText (caret, r.getX() + 12, r.getY() + 17, 20, 20, juce::Justification::centred);
        }

        // colour swatch
        const int swatchX = r.getX() + (expandable ? 38 : 16);
        g.setColour (l.tint);
        g.fillRoundedRectangle ((float) swatchX, (float) (r.getY() + 10), 10.0f, 34.0f, 3.0f);

        // name (+ child count for groups)
        const int nameX = swatchX + 22;
        g.setColour (kText);
        g.setFont (juce::FontOptions (15.0f, l.isGroup ? juce::Font::bold : juce::Font::plain));
        juce::String label = l.name;
        if (expandable) label << "   (" << (int) l.children.size() << ")";
        g.drawText (label, nameX, r.getY(), 220, r.getHeight(), juce::Justification::centredLeft, true);

        // simplified waveform on the right
        auto wave = juce::Rectangle<int> (nameX + 230, r.getY() + 12, r.getRight() - (nameX + 230) - 16, r.getHeight() - 24);
        if (wave.getWidth() > 40)
            drawWave (g, wave, l.tint, 48, 26.0f);

        // expanded children
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

                auto cwave = juce::Rectangle<int> (cr.getX() + 230, cr.getY() + 8, cr.getRight() - (cr.getX() + 230) - 12, cr.getHeight() - 16);
                if (cwave.getWidth() > 30)
                    drawWave (g, cwave, l.tint, 40, 14.0f);

                cy += kChildH + kChildGap;
            }
        }
    }

    // gentle footer hint
    g.setColour (kTextFaint);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Gruppen antippen zum Auf- und Zuklappen.",
                getLocalBounds().removeFromBottom (34),
                juce::Justification::centred, false);
}
