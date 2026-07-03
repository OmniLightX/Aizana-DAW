#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "ui/common/AudioThumbnailPool.h"

namespace te = tracktion::engine;

/**
 * AudioClipEditor — large waveform editor for ONE WaveAudioClip, docked at the
 * bottom (Studio One "Edit" view). Operates directly on the live clip object:
 *
 *   - Big waveform with its own horizontal zoom/scroll (independent of the
 *     arrangement zoom)
 *   - Edge trim (left/right 10px zones — same semantics as ClipComponent:
 *     left trim keeps source sync via offset, min length 50ms)
 *   - Fade handles (top corners, drag horizontally — te::IDs::fadeIn/fadeOut)
 *   - Clip gain slider in the header (te::IDs::gain, dB)
 *   - Click on the waveform = seek transport; playhead line follows playback
 *
 * All mutations go through the Edit's shared UndoManager — one transaction per
 * gesture. NO copied clip data: the arrangement shows every change immediately
 * via its own ValueTree listeners.
 */
class AudioClipEditor : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    AudioClipEditor (te::WaveAudioClip& clip, EditManager& editManager);
    ~AudioClipEditor() override;

    te::Clip& getClip() noexcept { return clip; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;
    void mouseMove  (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify   (const juce::MouseEvent& e, float scaleFactor) override;

private:
    te::WaveAudioClip& clip;
    EditManager&       editManager;

    // ---- Header widgets ----
    juce::Label  nameLabel;
    juce::Label  gainCaption;
    juce::Slider gainSlider;      // clip gain in dB (te::IDs::gain)
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomFitButton { "Fit" };

    // ---- View state: window into the clip, in clip-LOCAL seconds [0, len] ----
    double viewStartSecs = 0.0;
    double viewLenSecs   = 0.0;   // 0 = uninitialised → fit whole clip on first paint
    juce::ScrollBar hScroll { false };

    juce::SharedResourcePointer<AudioThumbnailPool> thumbnailPool;
    juce::AudioThumbnail thumbnail { 512, thumbnailPool->formatManager, thumbnailPool->cache };

    // ---- Gesture state ----
    enum class Drag { none, trimLeft, trimRight, fadeIn, fadeOut };
    Drag  activeDrag = Drag::none;
    int   dragStartScreenX     = 0;
    double dragStartClipStart  = 0.0;
    double dragStartClipEnd    = 0.0;
    double dragStartOffset     = 0.0;
    double dragStartFadeSecs   = 0.0;
    double trimMaxExtendLeft   = 0.0;
    double trimMaxExtendRight  = 0.0;

    static constexpr int kHeaderH   = 28;
    static constexpr int kScrollH   = 12;
    static constexpr int kEdgeZone  = 10;   // px trim zones
    static constexpr int kFadeGrab  = 14;   // px fade handle hit size

    juce::Rectangle<int> waveArea() const;
    double clipLenSecs() const;
    double sourceLenSecs();   // non-const: getAudioFile() is non-const in TE

    // clip-local time ↔ x pixel inside waveArea
    double xToLocalSecs (float x) const;
    float  localSecsToX (double t) const;

    void clampView();
    void zoomAround (double factor, float pivotX);
    void syncScrollBar();
    void syncGainSlider();

    // ValueTree::Listener — repaint on any clip property change (undo, arrangement edits)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

    void timerCallback() override;                     // playhead repaint
    void scrollBarMoved (juce::ScrollBar*, double) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipEditor)
};
