#include "ClipComponent.h"
#include "ArrangementView.h"
#include "app/MainComponent.h"
#include "engine/ClipEditor.h"

//==============================================================================

ClipComponent::ClipComponent (te::Clip& clip_, EditManager& editManager_,
                               ClipEditor& clipEditor_, double& pixelsPerSecond_,
                               const EditTool& activeTool)
    : clip (clip_), editManager (editManager_), clipEditor (clipEditor_),
      pixelsPerSecond (pixelsPerSecond_), activeTool_ (activeTool)
{
    setRepaintsOnMouseActivity (true);

    // Register standard audio formats so the thumbnail knows how to read WAV/AIFF/etc.
    formatManager.registerBasicFormats();

    // Load thumbnail from source file for WaveAudioClip
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (&clip))
    {
        const juce::File audioFile = waveClip->getSourceFileReference().getFile();
        if (audioFile.existsAsFile())
        {
            thumbnail.setSource (new juce::FileInputSource (audioFile));
            thumbnailLoaded = true;
        }
    }
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

    // ---- Waveform thumbnail (WaveAudioClip only) ----
    if (thumbnailLoaded && thumbnail.getTotalLength() > 0.0)
    {
        // Draw waveform in the interior area (inset from border)
        auto waveArea = bounds.reduced (2.0f, 12.0f).toNearestInt();  // 12px leaves room for name
        if (waveArea.getHeight() > 4)
        {
            // Semi-transparent white waveform over the clip body colour
            g.setColour (juce::Colours::white.withAlpha (0.35f));

            // Draw from the clip's source offset into the file so that split (offset) clips
            // show the correct portion of the waveform, not always the beginning of the file.
            const double clipOffset     = clip.getPosition().getOffset().inSeconds();
            const double clipLengthSecs = clip.getPosition().getLength().inSeconds();
            thumbnail.drawChannel (g,
                                   waveArea,
                                   clipOffset,                                   // start at source offset
                                   juce::jmax (0.001, clipOffset + clipLengthSecs), // end = offset + length
                                   0,                      // channel 0 (left/mono)
                                   1.0f);                  // vertical zoom = 1.0 (full amplitude)
        }
    }
}

void ClipComponent::mouseDown (const juce::MouseEvent& e)
{
    // Right-click: forward to ArrangementView for tool context menu
    if (e.mods.isPopupMenu())
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
            av->mouseDown (e.getEventRelativeTo (av));
        return;
    }

    if (! e.mods.isLeftButtonDown()) return;

    // Scissors tool: forward click to parent ArrangementView for clip splitting.
    // We must NOT start a drag — the drag would swallow the scissors action.
    if (activeTool_ == EditTool::Scissors)
    {
        if (auto* av = findParentComponentOfClass<ArrangementView>())
        {
            const auto eventInAV   = e.getEventRelativeTo (av);
            const int  timelineLeft = ArrangementView::kHeaderWidth;
            const int  xInTimeline  = eventInAV.x - timelineLeft
                                      + av->getTimelineViewportX();
            const double timeSecs = juce::jmax (0.0, (double)xInTimeline / av->getPixelsPerSecond());
            av->splitClipAtTime (timeSecs, eventInAV.y);
        }
        return;  // Do NOT start drag
    }

    // Normal pointer tool — start drag
    dragStartClipTimeSecs = clip.getPosition().getStart().inSeconds();
    dragStartMouseX = e.getEventRelativeTo (getParentComponent()).x;
    isDragging = false;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ClipComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown()) return;
    if (activeTool_ == EditTool::Scissors) return;  // scissors never drags clips
    isDragging = true;

    const int currentX = e.getEventRelativeTo (getParentComponent()).x;
    const int deltaX = currentX - dragStartMouseX;
    const double deltaSecs = deltaX / pixelsPerSecond;
    const double newStartSecs = juce::jmax (0.0, dragStartClipTimeSecs + deltaSecs);

    // Move clip position (undoable via ClipEditor)
    clipEditor.moveClip (clip, newStartSecs);

    // Update visual position immediately
    const double endSecs = newStartSecs + clip.getPosition().getLength().inSeconds();
    const int newX = (int)(newStartSecs * pixelsPerSecond);
    const int newW = juce::jmax (4, (int)((endSecs - newStartSecs) * pixelsPerSecond));
    setBounds (newX, getY(), newW, getHeight());
}

void ClipComponent::mouseUp (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    isDragging = false;
}

void ClipComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    // Double-click on a MIDI clip -> open Piano Roll in docked bottom panel
    if (auto* midiClip = dynamic_cast<te::MidiClip*> (&clip))
    {
        if (auto* mainComp = findParentComponentOfClass<MainComponent>())
            mainComp->showPianoRoll (*midiClip);
    }
    // Audio clips: no action in Phase 1 (future: waveform editor)
}
