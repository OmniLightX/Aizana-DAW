#include "ClipEditor.h"

// Convenience aliases for Tracktion's strong time types
using TimePos = tracktion::core::TimePosition;
using TimeRng = tracktion::core::TimeRange;
using TimeDur = tracktion::core::TimeDuration;

//==============================================================================
ClipEditor::ClipEditor (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
te::WaveAudioClip* ClipEditor::cutClip (te::WaveAudioClip& clip, double splitTimeSecs)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Cut clip");

    // Capture original clip bounds
    auto origStart  = clip.getPosition().getStart().inSeconds();
    auto origEnd    = clip.getPosition().getEnd().inSeconds();
    auto origOffset = clip.getPosition().getOffset().inSeconds();

    // Clamp split time to be within the clip
    splitTimeSecs = juce::jlimit (origStart, origEnd, splitTimeSecs);
    if (splitTimeSecs <= origStart || splitTimeSecs >= origEnd)
        return nullptr;  // degenerate split — nothing to do

    // Get the parent track
    auto* clipTrack = clip.getClipTrack();
    if (clipTrack == nullptr)
        return nullptr;

    auto* audioTrack = dynamic_cast<te::AudioTrack*> (clipTrack);
    if (audioTrack == nullptr)
        return nullptr;

    // ---- Left side: trim the original clip to [origStart, splitTime] ----
    // We leave the offset as-is (the left clip starts from the same place in the source file).
    {
        auto newStart = TimePos::fromSeconds (origStart);
        auto newEnd   = TimePos::fromSeconds (splitTimeSecs);
        clip.setPosition ({ TimeRng (newStart, newEnd), TimeDur::fromSeconds (origOffset) });
    }

    // ---- Right side: create a new clip at [splitTime, origEnd] ----
    // The right clip's offset into the source file = origOffset + (splitTime - origStart)
    double rightOffsetSecs = origOffset + (splitTimeSecs - origStart);

    auto rightStart = TimePos::fromSeconds (splitTimeSecs);
    auto rightEnd   = TimePos::fromSeconds (origEnd);
    te::ClipPosition rightPos { TimeRng (rightStart, rightEnd), {} };

    auto* newClipBase = audioTrack->insertNewClip (te::TrackItem::Type::wave,
                                                    "clip",
                                                    rightPos,
                                                    nullptr);
    auto* newClip = dynamic_cast<te::WaveAudioClip*> (newClipBase);
    if (newClip == nullptr)
        return nullptr;

    // Wire the new clip to the same source file as the original
    newClip->getSourceFileReference().setToDirectFileReference (clip.getCurrentSourceFile(), true);

    // Set the offset so the new clip plays from the correct position in the source file
    newClip->setPosition ({ TimeRng (rightStart, rightEnd),
                            TimeDur::fromSeconds (rightOffsetSecs) });

    return newClip;
}

//==============================================================================
void ClipEditor::trimClip (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Trim clip");

    // Compute how much the start moved to calculate the new offset
    double originalStartSecs = clip.getPosition().getStart().inSeconds();
    double originalOffsetSecs = clip.getPosition().getOffset().inSeconds();

    // Adjust the internal offset: if start moves right, we consume more of the source file
    double newOffsetSecs = originalOffsetSecs + (newStartTimeSecs - originalStartSecs);

    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newEndTimeSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd),
                        TimeDur::fromSeconds (newOffsetSecs) });
}

//==============================================================================
void ClipEditor::moveClip (te::Clip& clip, double newStartTimeSecs)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Move clip");

    double lengthSecs  = clip.getPosition().getLength().inSeconds();
    double offsetSecs  = clip.getPosition().getOffset().inSeconds();

    auto newStart = TimePos::fromSeconds (newStartTimeSecs);
    auto newEnd   = TimePos::fromSeconds (newStartTimeSecs + lengthSecs);
    clip.setPosition ({ TimeRng (newStart, newEnd),
                        TimeDur::fromSeconds (offsetSecs) });
}

//==============================================================================
te::Clip* ClipEditor::copyClip (te::Clip& sourceClip,
                                te::AudioTrack& destTrack,
                                double startTimeSecs)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return nullptr;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Copy clip");

    double lengthSecs  = sourceClip.getPosition().getLength().inSeconds();
    double offsetSecs  = sourceClip.getPosition().getOffset().inSeconds();

    auto newStart = TimePos::fromSeconds (startTimeSecs);
    auto newEnd   = TimePos::fromSeconds (startTimeSecs + lengthSecs);
    te::ClipPosition newPos { TimeRng (newStart, newEnd), {} };

    // Determine the clip type to create
    te::TrackItem::Type clipType = sourceClip.isMidi()
                                       ? te::TrackItem::Type::midi
                                       : te::TrackItem::Type::wave;

    auto* newClipBase = destTrack.insertNewClip (clipType, "clip", newPos, nullptr);
    if (newClipBase == nullptr)
        return nullptr;

    // For audio clips: wire to the same source file (non-destructive copy)
    if (auto* srcWave = dynamic_cast<te::WaveAudioClip*> (&sourceClip))
    {
        if (auto* newWave = dynamic_cast<te::WaveAudioClip*> (newClipBase))
        {
            newWave->getSourceFileReference().setToDirectFileReference (
                srcWave->getCurrentSourceFile(), true);
            // Preserve the source offset
            newWave->setPosition ({ TimeRng (newStart, newEnd),
                                    TimeDur::fromSeconds (offsetSecs) });
        }
    }

    return newClipBase;
}

//==============================================================================
te::WaveAudioClip* ClipEditor::importAudioFile (const juce::File& audioFile,
                                                 te::AudioTrack&   track,
                                                 double            startTimeSecs)
{
    // Determine the audio file duration via JUCE AudioFormatManager
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
    if (reader == nullptr)
        return nullptr;  // Unrecognised format

    double durationSecs = static_cast<double> (reader->lengthInSamples)
                          / reader->sampleRate;

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Import Audio: " + audioFile.getFileName());

    // Build the ClipPosition using TE v3.2.0 strong time types (STATE.md decision: plan 01-02)
    auto clipStart = TimePos::fromSeconds (startTimeSecs);
    auto clipEnd   = TimePos::fromSeconds (startTimeSecs + durationSecs);
    te::ClipPosition clipPos { TimeRng (clipStart, clipEnd), {} };

    auto* newClipBase = track.insertNewClip (te::TrackItem::Type::wave,
                                              audioFile.getFileNameWithoutExtension(),
                                              clipPos,
                                              nullptr);
    auto* newClip = dynamic_cast<te::WaveAudioClip*> (newClipBase);
    if (newClip == nullptr)
        return nullptr;

    // Point the clip at the source file non-destructively (STATE.md plan 01-03 decision:
    // use setToDirectFileReference(), not setCurrentSourceFile() which is protected)
    newClip->getSourceFileReference().setToDirectFileReference (audioFile, true);

    return newClip;
}

//==============================================================================
te::Clip* ClipEditor::duplicateClip (te::Clip& clip)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr) return nullptr;

    auto* audioTrack = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
    if (audioTrack == nullptr) return nullptr;

    double endTime = clip.getPosition().getEnd().inSeconds();

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Duplicate Clip");
    return copyClip (clip, *audioTrack, endTime);
}

//==============================================================================
void ClipEditor::copyToClipboard (te::Clip& clip)
{
    clipboardClip        = &clip;
    clipboardState       = clip.state.createCopy();
    clipboardSourceTrack = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
}

//==============================================================================
te::Clip* ClipEditor::pasteClip (te::AudioTrack& destTrack, double startTimeSecs)
{
    if (clipboardClip == nullptr || ! clipboardState.isValid())
        return nullptr;

    auto* edit = editManager.getEdit();
    if (edit == nullptr) return nullptr;

    editManager.getUndoManager().beginNewTransaction ("Paste Clip");
    return copyClip (*clipboardClip, destTrack, startTimeSecs);
}

//==============================================================================
void ClipEditor::setActiveTake (te::Clip& comp, int takeIndex)
{
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Set active take");
    comp.setCurrentTake (takeIndex);
}

//==============================================================================
int ClipEditor::getActiveTake (const te::Clip& comp) const
{
    return comp.getCurrentTake();
}
