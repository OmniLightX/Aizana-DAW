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
    if (editManager.getEdit() == nullptr)
        return nullptr;

    editManager.getUndoManager().beginNewTransaction ("Cut clip");

    // T1.3 Edit-Gruppen: der Schnitt sitzt auf allen zeitgleichen Clips der
    // Gruppen-Spuren (Multimic-Drums) — in DERSELBEN Transaktion = 1 Undo.
    // Companions VOR dem Zielschnitt einsammeln (der Cut ändert die Clip-Länge,
    // damit würde das Zeitgleichheits-Matching danach nicht mehr treffen).
    for (auto* companion : groupCompanions (clip))
        cutClipCore (*companion, splitTimeSecs);

    return cutClipCore (clip, splitTimeSecs);
}

//==============================================================================
// T1.3: Edit-Gruppen
//==============================================================================

static const juce::Identifier kEditGroupProp ("aizanaEditGroup");

int ClipEditor::getEditGroup (const te::Track& track)
{
    return (int) track.state.getProperty (kEditGroupProp, 0);
}

void ClipEditor::setEditGroup (te::Track& track, int groupId)
{
    groupId = juce::jlimit (0, kMaxEditGroups, groupId);
    auto& um = track.edit.getUndoManager();   // dieselbe geteilte UM des Edits
    um.beginNewTransaction (groupId == 0 ? juce::String ("Edit-Gruppe entfernen")
                                         : "Edit-Gruppe " + juce::String (groupId));
    if (groupId == 0)
        track.state.removeProperty (kEditGroupProp, &um);
    else
        track.state.setProperty (kEditGroupProp, groupId, &um);
}

juce::Array<te::AudioTrack*> ClipEditor::editGroupSiblings (te::Track& track)
{
    juce::Array<te::AudioTrack*> out;
    const int group = getEditGroup (track);
    if (group == 0)
        return out;

    for (auto* t : te::getAudioTracks (track.edit))
        if (t != &track && getEditGroup (*t) == group)
            out.add (t);
    return out;
}

juce::Array<te::WaveAudioClip*> ClipEditor::groupCompanions (te::WaveAudioClip& clip)
{
    juce::Array<te::WaveAudioClip*> out;
    auto* track = clip.getTrack();
    if (track == nullptr)
        return out;

    const double start = clip.getPosition().getStart().inSeconds();
    const double end   = clip.getPosition().getEnd().inSeconds();
    constexpr double eps = 0.001;   // 1 ms — Multimic-Aufnahmen starten samplegleich

    for (auto* sibling : editGroupSiblings (*track))
        for (auto* c : sibling->getClips())
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                if (std::abs (wc->getPosition().getStart().inSeconds() - start) < eps
                 && std::abs (wc->getPosition().getEnd().inSeconds()   - end)   < eps)
                    out.add (wc);
    return out;
}

// Transaction-free core (house pattern: cores never beginNewTransaction themselves,
// so composite gestures like removeRangeFromClip stay ONE undo step).
te::WaveAudioClip* ClipEditor::cutClipCore (te::WaveAudioClip& clip, double splitTimeSecs)
{
    if (editManager.getEdit() == nullptr)
        return nullptr;

    auto& undoManager = editManager.getUndoManager();

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
    if (editManager.getEdit() == nullptr)
        return;

    editManager.getUndoManager().beginNewTransaction ("Trim clip");
    trimClipCore (clip, newStartTimeSecs, newEndTimeSecs);
}

void ClipEditor::trimClipCore (te::Clip& clip, double newStartTimeSecs, double newEndTimeSecs)
{
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
te::WaveAudioClip* ClipEditor::removeRangeFromClip (te::WaveAudioClip& clip,
                                                    double rangeStartSecs, double rangeEndSecs)
{
    if (editManager.getEdit() == nullptr)
        return nullptr;

    const double cStart = clip.getPosition().getStart().inSeconds();
    const double cEnd   = clip.getPosition().getEnd().inSeconds();
    const double rStart = juce::jmax (cStart, juce::jmin (rangeStartSecs, rangeEndSecs));
    const double rEnd   = juce::jmin (cEnd,   juce::jmax (rangeStartSecs, rangeEndSecs));

    if (rEnd <= rStart)
        return nullptr;   // no overlap with the clip — nothing to remove

    // Range covers the whole clip → plain delete (single transaction of its own).
    if (rStart <= cStart && rEnd >= cEnd)
    {
        deleteClips ({ &clip });
        return nullptr;
    }

    // ONE transaction for the whole gesture (the S1 breath-edit workflow:
    // mark range → remove → gap stays, source sync preserved → single Cmd+Z).
    editManager.getUndoManager().beginNewTransaction ("Remove Range");

    if (rStart > cStart && rEnd < cEnd)
    {
        // Hole inside the clip: split at rStart (right piece spans [rStart, cEnd],
        // fades transfer correctly in the core), then trim the right piece's start
        // to rEnd — trimClipCore advances the offset, so the audio stays in sync.
        if (auto* right = cutClipCore (clip, rStart))
        {
            trimClipCore (*right, rEnd, cEnd);
            return right;
        }
        return nullptr;
    }

    if (rStart <= cStart)
        trimClipCore (clip, rEnd, cEnd);      // range cuts off the clip head
    else
        trimClipCore (clip, cStart, rStart);  // range cuts off the clip tail

    return nullptr;
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
te::WaveAudioClip* ClipEditor::bounceClipInPlace (te::WaveAudioClip& clip, bool withTrackFX)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr || &clip.edit != edit)
        return nullptr;

    auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
    if (track == nullptr)
        return nullptr;

    // Renderer's tracksToDo indexes into getAllTracks()
    const int trackIndex = te::getAllTracks (*edit).indexOf (track);
    if (trackIndex < 0)
        return nullptr;

    const auto range = clip.getPosition().time;
    if (range.getLength().inSeconds() <= 0.0)
        return nullptr;

    // Rendered files live next to the session; unsaved sessions use the temp dir
    const auto sessionFile = editManager.getCurrentFile();
    auto bounceDir = sessionFile.existsAsFile()
        ? sessionFile.getParentDirectory().getChildFile ("Bounces")
        : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AizanaBounces");
    bounceDir.createDirectory();

    const auto outFile = bounceDir
        .getChildFile (juce::File::createLegalFileName (clip.getName()) + " Bounce.wav")
        .getNonexistentSibling();

    double sampleRate = edit->engine.getDeviceManager().getSampleRate();
    if (sampleRate < 8000.0)
        sampleRate = 44100.0;   // headless / no device open

    te::Renderer::Parameters params (*edit);
    params.destFile           = outFile;
    params.audioFormat        = edit->engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth           = 24;
    params.sampleRateForAudio = sampleRate;
    params.blockSizeForAudio  = 512;
    params.time               = range;
    params.realTimeRender     = false;
    params.usePlugins         = withTrackFX;
    params.useMasterPlugins   = false;         // bounce ≠ mixdown — master stays live
    params.checkNodesForAudio = false;
    params.tracksToDo.setBit (trackIndex);
    params.allowedClips.add (&clip);   // only THIS event renders, siblings stay out

    const auto rendered = te::Renderer::renderToFile ("Bounce in Place", params);
    const auto resultFile = rendered.existsAsFile() ? rendered : params.destFile;
    if (! resultFile.existsAsFile())
        return nullptr;

    // Swap old clip for the render — ONE undo step (Cmd+Z restores the original;
    // fades/gain/FX are baked into the file, so the new clip starts clean).
    te::Clip::Ptr keepAlive (&clip);
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Bounce in Place");

    te::ClipPosition pos { range, {} };
    auto* newClipBase = track->insertNewClip (te::TrackItem::Type::wave,
                                              clip.getName() + " (Bounce)",
                                              pos, nullptr);
    auto* newClip = dynamic_cast<te::WaveAudioClip*> (newClipBase);
    if (newClip == nullptr)
        return nullptr;

    newClip->getSourceFileReference().setToDirectFileReference (resultFile, false);

    juce::ValueTree parent = keepAlive->state.getParent();
    if (parent.isValid())
        parent.removeChild (keepAlive->state, &um);

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
    deleteClips ({ &clip });
}

void ClipEditor::deleteClips (const juce::Array<te::Clip*>& clips)
{
    if (clips.isEmpty())
        return;

    // Keep every clip alive across the whole loop: removing a sibling's state
    // re-syncs the track's clip list, and a raw pointer into that list must not
    // dangle mid-batch.
    juce::Array<te::Clip::Ptr> keepAlive;
    for (auto* c : clips)
        if (c != nullptr)
            keepAlive.add (c);

    if (keepAlive.isEmpty())
        return;

    // T1.3 Edit-Gruppen: Delete propagiert auf zeitgleiche Clips der Gruppen-
    // Spuren (gleicher Batch = weiterhin EIN Undo). Dedupe über EditItemID.
    {
        juce::Array<te::EditItemID> seen;
        for (const auto& c : keepAlive)
            seen.add (c->itemID);

        const int originalCount = keepAlive.size();
        for (int i = 0; i < originalCount; ++i)
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (keepAlive.getReference (i).get()))
                for (auto* companion : groupCompanions (*wc))
                    if (! seen.contains (companion->itemID))
                    {
                        seen.add (companion->itemID);
                        keepAlive.add (companion);
                    }
    }

    // ONE transaction for the whole batch — deleting a multi-selection must be a
    // single Cmd+Z step (Studio One behaviour), not one step per clip.
    auto& undoManager = editManager.getUndoManager();
    undoManager.beginNewTransaction (keepAlive.size() == 1
                                        ? juce::String ("Delete clip")
                                        : "Delete " + juce::String (keepAlive.size()) + " clips");

    for (auto& clip : keepAlive)
    {
        // Remove the clip's ValueTree node from its parent (track state tree).
        // This is the canonical TE way to delete a clip — no TE API call needed.
        juce::ValueTree parent = clip->state.getParent();
        if (parent.isValid())
            parent.removeChild (clip->state, &undoManager);
    }
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
double ClipEditor::quantizedBeat (double beat, double grid,
                                  double strength, double swing) noexcept
{
    if (grid <= 0.0)
        return beat;

    strength = juce::jlimit (0.0, 1.0, strength);
    swing    = juce::jlimit (0.0, 1.0, swing);

    // Ziel einer Rasterlinie s: gerade Linien liegen exakt, ungerade werden um
    // swing·grid/2 nach hinten geschoben (S1/MPC-Konvention).
    auto targetFor = [grid, swing] (int64_t s) noexcept
    {
        double t = (double) s * grid;
        if ((s & 1) != 0)   // ungerade Rasterlinie (Zweierkomplement: gilt auch negativ)
            t += swing * grid * 0.5;
        return t;
    };

    const auto base = (int64_t) std::floor (beat / grid);
    double best = targetFor (base);
    double bestDist = std::abs (beat - best);

    for (const auto s : { base - 1, base + 1, base + 2 })
    {
        const double t = targetFor (s);
        const double d = std::abs (beat - t);
        // <= : bei exakter Mitte gewinnt das SPÄTERE Ziel (Aufrunden, wie std::round)
        if (d < bestDist || (d == bestDist && t > best)) { bestDist = d; best = t; }
    }

    return beat + (best - beat) * strength;
}

int ClipEditor::quantizeMidiClip (te::MidiClip& clip, double gridBeats,
                                  double strength, double swing,
                                  const juce::Array<te::MidiNote*>* subset)
{
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Quantize");

    auto notesToDo = [&]() -> juce::Array<te::MidiNote*>
    {
        if (subset != nullptr)
            return *subset;
        juce::Array<te::MidiNote*> all;
        for (auto* n : clip.getSequence().getNotes())
            all.add (n);
        return all;
    }();

    int moved = 0;
    for (auto* n : notesToDo)
    {
        if (n == nullptr)
            continue;

        const double oldStart = n->getStartBeat().inBeats();
        const double newStart = juce::jmax (0.0, quantizedBeat (oldStart, gridBeats,
                                                                strength, swing));
        if (std::abs (newStart - oldStart) < 1.0e-9)
            continue;

        n->setStartAndLength (tracktion::core::BeatPosition::fromBeats (newStart),
                              n->getLengthBeats(), &um);
        ++moved;
    }

    return moved;
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
