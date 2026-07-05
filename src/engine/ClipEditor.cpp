#include "ClipEditor.h"
#include "CompingEngine.h"

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

    // Capture fade state before split — needed to correctly transfer fades to right clip.
    // After the left clip is trimmed, its inherited fadeOut would be measured from the new
    // (shorter) end, positioning the fade at the wrong absolute timeline location.
    const double origFadeOut     = (double)clip.state.getProperty (te::IDs::fadeOut,     0.0);
    const double origFadeIn      = (double)clip.state.getProperty (te::IDs::fadeIn,      0.0);
    const int    origFadeOutType = (int)(double)clip.state.getProperty (te::IDs::fadeOutType, 0);

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
    // The right clip's offset into the source file = origOffset + (splitTime - origStart).
    // Pass the correct offset directly to insertNewClip so there is never an intermediate
    // zero-offset state in the TE data model (which caused orphan state when origOffset > 0).
    double rightOffsetSecs = origOffset + (splitTimeSecs - origStart);

    auto rightStart = TimePos::fromSeconds (splitTimeSecs);
    auto rightEnd   = TimePos::fromSeconds (origEnd);
    te::ClipPosition rightPos { TimeRng (rightStart, rightEnd),
                                TimeDur::fromSeconds (rightOffsetSecs) };

    auto* newClipBase = audioTrack->insertNewClip (te::TrackItem::Type::wave,
                                                    "clip",
                                                    rightPos,
                                                    nullptr);
    auto* newClip = dynamic_cast<te::WaveAudioClip*> (newClipBase);
    if (newClip == nullptr)
        return nullptr;

    // Wire the new clip to the same source file as the original.
    // setToDirectFileReference does not affect timeline position or offset.
    // Use absolute path (false) — relative-path mode requires both filePathResolver AND a
    // resolvable relative path between the edit dir and audio file; absolute is always safe.
    newClip->getSourceFileReference().setToDirectFileReference (clip.getCurrentSourceFile(), false);

    // Reaffirm position in case insertNewClip did not persist the offset from ClipPosition.
    // This is a no-op when TE respects ClipPosition.offset; it is the authoritative
    // correction when TE ignores it (some TE versions reset offset to 0 on insert).
    newClip->setPosition ({ TimeRng (rightStart, rightEnd),
                            TimeDur::fromSeconds (rightOffsetSecs) });

    // ---- Fade correction after split ----
    // Studio One 7 standard: the clip that retains the original edge keeps the fade;
    // the clip that lost the edge loses it.
    //   - LEFT clip (original, now ending at splitTimeSecs) inherits fadeOut but it now
    //     measures from the wrong end. Fix: clear or truncate, and transfer to RIGHT clip.
    //   - RIGHT clip owns origEnd, so it should carry the original fadeOut.
    //   - LEFT clip owns origStart, so it keeps fadeIn (just clamp if oversized).

    if (origFadeOut > 0.0)
    {
        // Absolute timeline position where the original fade region began
        const double fadeOutStartSecs = origEnd - origFadeOut;

        if (fadeOutStartSecs >= splitTimeSecs)
        {
            // Entire fade region is in the RIGHT clip — clear from LEFT, set on RIGHT
            clip.state.setProperty     (te::IDs::fadeOut,     0.0,             &undoManager);
            newClip->state.setProperty (te::IDs::fadeOut,     origFadeOut,     &undoManager);
            newClip->state.setProperty (te::IDs::fadeOutType, origFadeOutType, &undoManager);
        }
        else
        {
            // Fade region starts in LEFT clip, extends to (or past) the split point.
            // LEFT clip: truncate fade to the portion that fits within [origStart, splitTimeSecs].
            const double newFadeOut = juce::jmax (0.0, splitTimeSecs - fadeOutStartSecs);
            clip.state.setProperty (te::IDs::fadeOut, newFadeOut, &undoManager);
            // RIGHT clip: keep original fade — it owns origEnd so the fade is still correct.
            newClip->state.setProperty (te::IDs::fadeOut,     origFadeOut,     &undoManager);
            newClip->state.setProperty (te::IDs::fadeOutType, origFadeOutType, &undoManager);
        }
    }

    // fadeIn: measured from origStart forward — LEFT clip owns origStart so it keeps it.
    // Clamp to left clip duration if oversized (prevents fade extending past new clip end).
    if (origFadeIn > 0.0)
    {
        const double leftDuration = splitTimeSecs - origStart;
        if (origFadeIn > leftDuration)
            clip.state.setProperty (te::IDs::fadeIn, leftDuration, &undoManager);
    }
    // RIGHT clip (new blank clip) has no fadeIn by default — correct.

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
void ClipEditor::beginMoveTransaction()
{
    editManager.getUndoManager().beginNewTransaction ("Move Clip");
}

void ClipEditor::moveClip (te::Clip& clip, double newStartTimeSecs)
{
    // No beginNewTransaction here — the caller is responsible for the undo boundary.
    // For drag moves, call beginMoveTransaction() in mouseDown so that the entire
    // drag resolves to a single Cmd+Z step.
    double lengthSecs = clip.getPosition().getLength().inSeconds();
    double offsetSecs = clip.getPosition().getOffset().inSeconds();

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
                srcWave->getCurrentSourceFile(), false);
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

    // Store absolute path — TE's relative-path mode requires both editFileRetriever AND
    // filePathResolver to be set; without filePathResolver, findFileFromString() falls back
    // to getEditFileFromProjectManager() which returns an empty File in unsaved sessions,
    // making getFile() return an invalid path → no waveform, no audio.
    newClip->getSourceFileReference().setToDirectFileReference (audioFile, false);

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
void ClipEditor::moveClipToTrack (te::Clip& clip, te::AudioTrack& destTrack)
{
    if (clip.getClipTrack() == &destTrack)
        return;  // already on target track

    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Move clip to track");
    clip.moveTo (destTrack);
}

void ClipEditor::deleteClip (te::Clip& clip)
{
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Delete clip");
    // Remove the clip's ValueTree node from its parent (track state tree).
    // This is the canonical TE way to delete a clip — no TE API call needed.
    juce::ValueTree parent = clip.state.getParent();
    if (parent.isValid())
        parent.removeChild (clip.state, &undoManager);
}

void ClipEditor::setActiveTake (te::Clip& comp, int takeIndex)
{
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction ("Set active take");

    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (&comp))
    {
        auto takes = wc->state.getChildWithName (te::IDs::TAKES);
        auto take  = takes.getChild (takeIndex);
        const auto id = te::ProjectItemID::fromProperty (take, te::IDs::source);

        if (take.isValid() && wc->edit.engine.getProjectManager().getProjectItem (id) == nullptr)
        {
            // Direct-file take (project-less loop recording): TE's setCurrentTake
            // REMOVES a take whose ProjectItemID can't be resolved — switch the
            // clip's source string directly instead (the project-less equivalent).
            // An active Aizana comp would keep overriding the audio, so drop it.
            auto compTree = wc->state.getChildWithName (CompingEngine::compType);
            if (compTree.isValid())
                wc->state.removeChild (compTree, &undoManager);
            CompingEngine::setSourceOverride (*wc, {});
            wc->state.setProperty (te::IDs::source,
                                   take.getProperty (te::IDs::source).toString(),
                                   &undoManager);
            return;
        }
    }

    comp.setCurrentTake (takeIndex);
}

//==============================================================================
int ClipEditor::getActiveTake (const te::Clip& comp) const
{
    // For direct-file takes (project-less recordings) TE's getCurrentTake()
    // can't resolve ProjectItemIDs — match the take sources directly instead.
    if (auto* wc = dynamic_cast<const te::WaveAudioClip*> (&comp))
    {
        const int idx = CompingEngine::activeTakeIndexFor (*wc);
        if (idx >= 0)
            return idx;
    }
    return comp.getCurrentTake();
}

//==============================================================================
bool ClipEditor::conformToSongTempo (te::WaveAudioClip& clip, double sourceBpm)
{
    if (sourceBpm < 30.0 || sourceBpm > 1000.0)
        return false;

    auto& edit = clip.edit;
    const auto file = clip.getSourceFileReference().getFile();
    const auto info = te::AudioFile (edit.engine, file).getInfo();
    if (info.sampleRate <= 0.0)
        return false;

    const auto pos     = clip.getPosition();
    const auto songBpm = edit.tempoSequence.getBpmAt (pos.getStart());
    if (songBpm <= 0.0)
        return false;

    // One gesture = one undo step: loopInfo BPM (source tempo), stretch mode,
    // autoTempo, and the rescaled clip length together.
    editManager.getUndoManager().beginNewTransaction ("Conform clip to song tempo");

    clip.getLoopInfo().setBpm (sourceBpm, info);
    clip.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
    clip.setAutoTempo (true);

    // The same musical material now lasts oldLength * source/song: material
    // recorded faster than the song stretches out, and vice versa. Clamp to
    // the stretched source's available length.
    const auto newLength = juce::jmin (pos.getLength() * (sourceBpm / songBpm),
                                       clip.getMaximumLength());
    if (newLength > tracktion::TimeDuration())
        clip.setLength (newLength, true);

    return true;
}

//==============================================================================
bool ClipEditor::removeSongTempoConform (te::WaveAudioClip& clip)
{
    if (! clip.getAutoTempo())
        return false;

    auto& edit = clip.edit;
    const auto file = clip.getSourceFileReference().getFile();
    const auto info = te::AudioFile (edit.engine, file).getInfo();

    const auto pos       = clip.getPosition();
    const auto songBpm   = edit.tempoSequence.getBpmAt (pos.getStart());
    const auto sourceBpm = info.sampleRate > 0.0 ? clip.getLoopInfo().getBpm (info) : 0.0;

    editManager.getUndoManager().beginNewTransaction ("Remove song tempo conform");

    clip.setAutoTempo (false);
    clip.setTimeStretchMode (te::TimeStretcher::disabled);

    // Restore the natural playback length (inverse of the conform factor).
    if (sourceBpm > 0.0 && songBpm > 0.0)
    {
        const auto newLength = juce::jmin (pos.getLength() * (songBpm / sourceBpm),
                                           clip.getMaximumLength());
        if (newLength > tracktion::TimeDuration())
            clip.setLength (newLength, true);
    }

    return true;
}
