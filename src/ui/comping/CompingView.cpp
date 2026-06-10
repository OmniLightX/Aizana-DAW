#include "CompingView.h"

namespace
{
    const juce::Colour kBackground    (0xff141414);
    const juce::Colour kHeaderBg      (0xff1a1a1a);
    const juce::Colour kLaneBg        (0xff1e1e1e);
    const juce::Colour kLaneBgAlt     (0xff232323);
    const juce::Colour kLabelBg       (0xff262626);
    const juce::Colour kBorder        (0xff3a3a3a);
    const juce::Colour kWaveform      (0xff6a8caf);
    const juce::Colour kCompFill      (0x3344cc66);   // translucent green: comp uses this lane
    const juce::Colour kCompEdge      (0xff44cc66);
    const juce::Colour kGhostFill     (0x33ffaa00);   // drag preview
    const juce::Colour kTextDim       (0xff999999);
    const juce::Colour kText          (0xffdddddd);
}

//==============================================================================
CompingView::CompingView (te::WaveAudioClip& clip, EditManager& em, CompingEngine& engine)
    : clipPtr (&clip), editManager (em), compingEngine (engine)
{
    formatManager.registerBasicFormats();

    takeFiles  = compingEngine.getTakeFiles (clip);
    numTakes   = takeFiles.size();
    compLength = compingEngine.getCompLength (clip);
    rebuildThumbnails();

    clearCompButton.setButtonText (juce::String::fromUTF8 ("Comp auflösen"));
    clearCompButton.setTooltip (juce::String::fromUTF8 (
        "Entfernt den Comp — der Clip spielt wieder den zuletzt gewählten Take."));
    clearCompButton.onClick = [this]
    {
        if (auto* wc = getClip())
            compingEngine.clearComp (*wc);
    };
    addAndMakeVisible (clearCompButton);

    clip.state.addListener (this);
}

CompingView::~CompingView()
{
    if (auto* wc = getClip())
        wc->state.removeListener (this);
}

te::WaveAudioClip* CompingView::getClip() const
{
    return dynamic_cast<te::WaveAudioClip*> (clipPtr.get());
}

void CompingView::rebuildThumbnails()
{
    thumbnails.clear();
    for (const auto& f : takeFiles)
    {
        auto* t = thumbnails.add (new juce::AudioThumbnail (512, formatManager, thumbnailCache));
        if (f.existsAsFile())
            t->setSource (new juce::FileInputSource (f));
    }
}

//==============================================================================
juce::Rectangle<int> CompingView::getLaneArea() const
{
    return getLocalBounds().withTrimmedTop (kHeaderHeight);
}

juce::Rectangle<int> CompingView::getLaneBounds (int laneIndex) const
{
    auto area = getLaneArea();
    if (numTakes <= 0)
        return {};
    const int laneHeight = area.getHeight() / numTakes;
    return { area.getX(), area.getY() + laneIndex * laneHeight, area.getWidth(), laneHeight };
}

int CompingView::laneIndexAt (juce::Point<int> p) const
{
    auto area = getLaneArea();
    if (numTakes <= 0 || ! area.contains (p))
        return -1;
    const int laneHeight = juce::jmax (1, area.getHeight() / numTakes);
    return juce::jmin (numTakes - 1, (p.y - area.getY()) / laneHeight);
}

double CompingView::xToTime (int x) const
{
    const auto wave = getLaneArea().withTrimmedLeft (kLabelWidth);
    if (wave.getWidth() <= 0 || compLength <= 0.0)
        return 0.0;
    return juce::jlimit (0.0, compLength,
                         (x - wave.getX()) / (double) wave.getWidth() * compLength);
}

int CompingView::timeToX (double t) const
{
    const auto wave = getLaneArea().withTrimmedLeft (kLabelWidth);
    if (compLength <= 0.0)
        return wave.getX();
    return wave.getX() + (int) std::lround (t / compLength * wave.getWidth());
}

//==============================================================================
void CompingView::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto* wc = getClip();

    // ---- Header ----
    auto header = getLocalBounds().removeFromTop (kHeaderHeight);
    g.setColour (kHeaderBg);
    g.fillRect (header);
    g.setColour (kBorder);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

    g.setColour (kText);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const juce::String title = juce::String::fromUTF8 ("Comping — ")
                                 + (wc != nullptr ? wc->getName() : juce::String());
    g.drawText (title, header.reduced (10, 0).removeFromLeft (300),
                juce::Justification::centredLeft, true);

    g.setColour (kTextDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String::fromUTF8 (
                    "Über eine Take-Spur ziehen = Bereich aus diesem Take · Klick = Abschnitt wechseln"),
                header.reduced (10, 0).withTrimmedLeft (310).withTrimmedRight (130),
                juce::Justification::centredLeft, true);

    if (wc == nullptr || numTakes == 0)
        return;

    const auto sections = compingEngine.getSections (*wc);
    const int activeTake = compingEngine.getActiveTakeIndex (*wc);
    const bool compActive = ! sections.isEmpty();

    // ---- Lanes ----
    for (int lane = 0; lane < numTakes; ++lane)
    {
        auto bounds = getLaneBounds (lane);
        auto label  = bounds.removeFromLeft (kLabelWidth);
        auto wave   = bounds;

        g.setColour (lane % 2 == 0 ? kLaneBg : kLaneBgAlt);
        g.fillRect (wave);
        g.setColour (kLabelBg);
        g.fillRect (label);

        // Label: take number + marker for the whole-take selection (no comp)
        g.setColour ((! compActive && lane == activeTake) ? kCompEdge : kText);
        g.setFont (juce::FontOptions (12.0f));
        juce::String name ("Take " + juce::String (lane + 1));
        if (! compActive && lane == activeTake)
            name << juce::String::fromUTF8 ("  ●");
        g.drawText (name, label.reduced (8, 0), juce::Justification::centredLeft, true);

        // Waveform
        if (auto* thumb = thumbnails[lane])
        {
            if (thumb->getTotalLength() > 0.0)
            {
                g.setColour (kWaveform);
                thumb->drawChannels (g, wave.reduced (0, 3), 0.0, compLength, 0.9f);
            }
            else
            {
                g.setColour (kTextDim);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (juce::String::fromUTF8 ("Quelldatei nicht gefunden"),
                            wave, juce::Justification::centred, false);
            }
        }

        // Comp sections taken from this lane
        if (compActive)
        {
            double start = 0.0;
            for (const auto& s : sections)
            {
                if (s.takeIndex == lane)
                {
                    const int x0 = timeToX (start);
                    const int x1 = timeToX (s.endTime);
                    juce::Rectangle<int> r (x0, wave.getY(), juce::jmax (2, x1 - x0), wave.getHeight());
                    g.setColour (kCompFill);
                    g.fillRect (r);
                    g.setColour (kCompEdge);
                    g.drawRect (r, 1);
                }
                start = s.endTime;
            }
        }

        g.setColour (kBorder);
        g.drawHorizontalLine (getLaneBounds (lane).getBottom() - 1, 0.0f, (float) getWidth());
    }

    // Section boundary lines across all lanes (cut markers)
    if (compActive)
    {
        g.setColour (kBorder.brighter (0.2f));
        double start = 0.0;
        for (const auto& s : sections)
        {
            if (start > 0.0)
            {
                const int x = timeToX (start);
                g.drawVerticalLine (x, (float) kHeaderHeight, (float) getHeight());
            }
            start = s.endTime;
        }
    }

    // ---- Drag ghost ----
    if (dragging && dragLane >= 0)
    {
        auto laneB = getLaneBounds (dragLane).withTrimmedLeft (kLabelWidth);
        const int x0 = timeToX (juce::jmin (dragStartTime, dragCurrentTime));
        const int x1 = timeToX (juce::jmax (dragStartTime, dragCurrentTime));
        g.setColour (kGhostFill);
        g.fillRect (juce::Rectangle<int> (x0, laneB.getY(), juce::jmax (2, x1 - x0), laneB.getHeight()));
    }
}

void CompingView::resized()
{
    clearCompButton.setBounds (getWidth() - 130, 3, 120, kHeaderHeight - 6);
}

//==============================================================================
void CompingView::mouseDown (const juce::MouseEvent& e)
{
    dragLane = laneIndexAt (e.getPosition());
    dragging = false;
    if (dragLane < 0)
        return;

    // Click on the lane label = use the whole take
    if (e.getPosition().x < kLabelWidth)
    {
        if (auto* wc = getClip())
            compingEngine.selectWholeTake (*wc, dragLane);
        dragLane = -1;
        return;
    }

    dragStartTime = dragCurrentTime = xToTime (e.getPosition().x);
}

void CompingView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0)
        return;
    if (e.getDistanceFromDragStart() > 3)
        dragging = true;
    dragCurrentTime = xToTime (e.getPosition().x);
    repaint();
}

void CompingView::mouseUp (const juce::MouseEvent& e)
{
    if (dragLane < 0)
        return;

    auto* wc = getClip();
    if (wc != nullptr)
    {
        if (dragging)
            compingEngine.applyTakeSection (*wc,
                                            juce::jmin (dragStartTime, dragCurrentTime),
                                            juce::jmax (dragStartTime, dragCurrentTime),
                                            dragLane);
        else
            compingEngine.switchSectionAt (*wc, xToTime (e.getPosition().x), dragLane);
    }

    dragLane = -1;
    dragging = false;
    repaint();
}

//==============================================================================
void CompingView::valueTreeParentChanged (juce::ValueTree& tree)
{
    // Clip removed from its track (delete or undo of the recording) → close.
    if (auto* wc = getClip())
        if (tree == wc->state && ! tree.getParent().isValid())
            if (onCloseRequest)
                onCloseRequest();
}
