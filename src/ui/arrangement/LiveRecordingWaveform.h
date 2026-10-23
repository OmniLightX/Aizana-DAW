#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>

namespace LiveRecordingWaveform
{
// Message-thread only. The engine's recording writer fills this thumbnail;
// drawing never reads the unfinished audio file or changes recording state.
inline bool draw (juce::Graphics& g, tracktion::engine::AudioTrack& track,
                  juce::Rectangle<int> viewport, double pixelsPerSecond, double scrollX)
{
    namespace te = tracktion::engine;
    auto& transport = track.edit.getTransport();
    if (! transport.isRecording() || pixelsPerSecond <= 0.0 || viewport.isEmpty())
        return false;
    auto* context = transport.getCurrentPlaybackContext();
    if (context == nullptr) return false;

    bool drawn = false;
    for (auto* input : track.edit.getEditInputDevices().getDevicesForTargetTrack (track))
    {
        if (! input->isRecording (track.itemID)) continue;
        const auto file = input->getRecordingFile (track.itemID);
        if (file == juce::File()) continue;
        auto thumbnail = track.edit.engine.getRecordingThumbnailManager().getThumbnailFor (file);
        const double length = thumbnail->thumb->getTotalLength();
        if (length <= 0.0) continue;
        const double origin = thumbnail->punchInTime.inSeconds();
        double start = origin, end = origin + length, offset = 0.0;
        const auto loop = context->getLoopTimes();
        const double loopStart = loop.getStart().inSeconds();
        const double loopEnd = loop.getEnd().inSeconds();
        const double loopLength = loop.getLength().inSeconds();
        if (context->isLooping() && loopLength > 0.0 && end >= loopEnd)
        {
            const double passes = std::floor ((end - loopStart) / loopLength);
            offset = passes * loopLength;
            start = loopStart;
            end -= offset;
        }
        else if (track.edit.recordingPunchInOut)
        {
            start = std::max (start, transport.getLoopRange().getStart().inSeconds());
            end = std::min (end, transport.getLoopRange().getEnd().inSeconds());
        }
        // Clip the time range before converting to pixels (long recordings,
        // zoomed views, and horizontal scrolling must stay bounded).
        start = std::max (start, scrollX / pixelsPerSecond);
        end = std::min (end, (scrollX + viewport.getWidth()) / pixelsPerSecond);
        if (end <= start) continue;
        const int x1 = viewport.getX() + juce::roundToInt (start * pixelsPerSecond - scrollX);
        const int x2 = viewport.getX() + juce::roundToInt (end * pixelsPerSecond - scrollX);
        const auto bounds = juce::Rectangle<int> (x1, viewport.getY(), x2 - x1, viewport.getHeight());
        if (bounds.isEmpty()) continue;
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (viewport);
        g.setColour (juce::Colour (0xe63a9bdc));
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
        g.setColour (juce::Colour (0xffdcf6ff));
        thumbnail->thumb->drawChannels (g, bounds.reduced (0, 3),
                                       start + offset - origin, end + offset - origin, 1.0f);
        g.setColour (juce::Colour (0xff56c6f0));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 3.0f, 1.0f);
        drawn = true;
    }
    return drawn;
}
}
