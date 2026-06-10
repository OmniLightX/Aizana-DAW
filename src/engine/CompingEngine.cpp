#include "CompingEngine.h"

//==============================================================================
// te::Clip::setCurrentSourceFile is protected and TE only friends its own
// WaveCompManager. Explicit template instantiation is exempt from access
// checks ([temp.explicit]/12) — the standard way to lift a member pointer
// without patching the vendored submodule. A TE signature change here becomes
// a compile error, never a silent break.
namespace cloudes_access
{
    using SetSourceFn = void (te::Clip::*) (const juce::File&);
    SetSourceFn setCurrentSourceFileFn();

    template <SetSourceFn Fn>
    struct Robber { friend SetSourceFn setCurrentSourceFileFn() { return Fn; } };
    template struct Robber<&te::Clip::setCurrentSourceFile>;
}

void CompingEngine::setSourceOverride (te::Clip& clip, const juce::File& file)
{
    (clip.*cloudes_access::setCurrentSourceFileFn()) (file);
}

//==============================================================================
const juce::Identifier CompingEngine::compType      ("CLOUDESCOMP");
const juce::Identifier CompingEngine::sectionType   ("SECTION");
const juce::Identifier CompingEngine::takeIndexProp ("takeIndex");
const juce::Identifier CompingEngine::endTimeProp   ("endTime");

namespace
{
    constexpr double kMinSectionSecs = 0.005;   // sections shorter than this are merged away
    constexpr double kCrossfadeSecs  = 0.010;   // equal-power crossfade at section boundaries
    constexpr int    kDebounceMs     = 300;

    double readFileDurationSeconds (juce::AudioFormatManager& afm, const juce::File& f)
    {
        if (! f.existsAsFile())
            return 0.0;
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (afm.createReaderFor (f)))
            if (reader->sampleRate > 0.0)
                return (double) reader->lengthInSamples / reader->sampleRate;
        return 0.0;
    }
}

//==============================================================================
CompingEngine::CompingEngine (EditManager& em) : editManager (em) {}

CompingEngine::~CompingEngine()
{
    *alive = false;
    stopTimer();
    renderPool.removeAllJobs (true, 4000);
    if (attachedRoot.isValid())
        attachedRoot.removeListener (this);
}

//==============================================================================
void CompingEngine::attachToEdit()
{
    if (attachedRoot.isValid())
        attachedRoot.removeListener (this);
    attachedRoot = {};
    pendingClips.clear();

    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    attachedRoot = edit->state;
    attachedRoot.addListener (this);

    // Re-render every active comp after load — setCurrentSourceFile is transient,
    // so a freshly loaded session plays the raw take until we re-apply the comp.
    for (auto* track : te::getAudioTracks (*edit))
        for (auto* clip : track->getClips())
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (clip))
                if (hasComp (*wc))
                    scheduleRenderForClipState (wc->state);
}

//==============================================================================
juce::ValueTree CompingEngine::getCompTree (const te::WaveAudioClip& clip) const
{
    return clip.state.getChildWithName (compType);
}

juce::ValueTree CompingEngine::getOrCreateCompTree (te::WaveAudioClip& clip, juce::UndoManager& um)
{
    auto existing = getCompTree (clip);
    if (existing.isValid())
        return existing;

    // Seed the new comp with the currently active take across the whole range,
    // so starting to comp never silences material outside the first drag.
    juce::ValueTree comp (compType);
    juce::ValueTree section (sectionType);
    section.setProperty (takeIndexProp, juce::jmax (0, getActiveTakeIndex (clip)), nullptr);
    section.setProperty (endTimeProp, getCompLength (clip), nullptr);
    comp.addChild (section, -1, nullptr);
    clip.state.addChild (comp, -1, &um);
    return comp;
}

bool CompingEngine::hasComp (const te::WaveAudioClip& clip) const
{
    return getCompTree (clip).isValid();
}

juce::Array<CompingEngine::Section> CompingEngine::readSections (const juce::ValueTree& compTree) const
{
    juce::Array<Section> out;
    for (const auto& child : compTree)
        if (child.hasType (sectionType))
            out.add ({ (int) child.getProperty (takeIndexProp, -1),
                       (double) child.getProperty (endTimeProp, 0.0) });
    return out;
}

juce::Array<CompingEngine::Section> CompingEngine::getSections (const te::WaveAudioClip& clip) const
{
    auto comp = getCompTree (clip);
    return comp.isValid() ? readSections (comp) : juce::Array<Section>();
}

void CompingEngine::writeSections (juce::ValueTree& compTree,
                                   const juce::Array<Section>& sections,
                                   juce::UndoManager& um)
{
    compTree.removeAllChildren (&um);
    for (const auto& s : sections)
    {
        juce::ValueTree section (sectionType);
        section.setProperty (takeIndexProp, s.takeIndex, nullptr);
        section.setProperty (endTimeProp, s.endTime, nullptr);
        compTree.addChild (section, -1, &um);
    }
}

//==============================================================================
juce::Array<juce::File> CompingEngine::getTakeFiles (const te::WaveAudioClip& clip) const
{
    juce::Array<juce::File> files;
    auto takes = clip.state.getChildWithName (te::IDs::TAKES);
    for (const auto& take : takes)
    {
        const auto src = take.getProperty (te::IDs::source).toString();
        files.add (te::SourceFileReference::findFileFromString (clip.edit, src));
    }
    return files;
}

double CompingEngine::getCompLength (const te::WaveAudioClip& clip) const
{
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();

    double longest = 0.0;
    for (const auto& f : getTakeFiles (clip))
        longest = juce::jmax (longest, readFileDurationSeconds (afm, f));
    return longest;
}

int CompingEngine::activeTakeIndexFor (const te::WaveAudioClip& clip)
{
    const auto currentSrc = clip.state.getProperty (te::IDs::source).toString();
    const auto currentFile = te::SourceFileReference::findFileFromString (clip.edit, currentSrc);

    auto takes = clip.state.getChildWithName (te::IDs::TAKES);
    for (int i = 0; i < takes.getNumChildren(); ++i)
    {
        const auto src = takes.getChild (i).getProperty (te::IDs::source).toString();
        if (src == currentSrc)
            return i;
        // Mixed relative/absolute references → compare the resolved files
        if (currentFile != juce::File()
            && te::SourceFileReference::findFileFromString (clip.edit, src) == currentFile)
            return i;
    }
    return -1;
}

//==============================================================================
void CompingEngine::applyTakeSection (te::WaveAudioClip& clip, double t0, double t1, int takeIndex)
{
    const double len = getCompLength (clip);
    if (len <= 0.0)
        return;

    const double a = juce::jlimit (0.0, len, juce::jmin (t0, t1));
    const double b = juce::jlimit (0.0, len, juce::jmax (t0, t1));
    if (b - a < kMinSectionSecs)
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Comping");
    auto comp = getOrCreateCompTree (clip, um);
    auto sections = readSections (comp);

    // Piecewise-constant overwrite: f'(t) = takeIndex on [a,b], else old f(t).
    auto takeAt = [&sections] (double t) -> int
    {
        for (const auto& s : sections)
            if (t < s.endTime)
                return s.takeIndex;
        return sections.isEmpty() ? -1 : sections.getLast().takeIndex;
    };

    juce::Array<double> cuts;
    for (const auto& s : sections)
        if (s.endTime < a - kMinSectionSecs || s.endTime > b + kMinSectionSecs)
            cuts.add (s.endTime);
    cuts.add (a);
    cuts.add (b);
    cuts.add (len);
    std::sort (cuts.begin(), cuts.end());

    juce::Array<Section> out;
    double prev = 0.0;
    for (double c : cuts)
    {
        if (c - prev < kMinSectionSecs)
            continue;
        const double mid = (prev + c) * 0.5;
        const int idx = (mid >= a && mid < b) ? takeIndex : takeAt (mid);
        if (! out.isEmpty() && out.getLast().takeIndex == idx)
            out.getReference (out.size() - 1).endTime = c;
        else
            out.add ({ idx, c });
        prev = c;
    }

    writeSections (comp, out, um);
}

void CompingEngine::switchSectionAt (te::WaveAudioClip& clip, double time, int takeIndex)
{
    auto sections = getSections (clip);
    if (sections.isEmpty())
    {
        // No comp yet — the "section" is the whole clip.
        applyTakeSection (clip, 0.0, getCompLength (clip), takeIndex);
        return;
    }

    double start = 0.0;
    for (const auto& s : sections)
    {
        if (time < s.endTime)
        {
            applyTakeSection (clip, start, s.endTime, takeIndex);
            return;
        }
        start = s.endTime;
    }
}

void CompingEngine::clearComp (te::WaveAudioClip& clip)
{
    auto comp = getCompTree (clip);
    if (! comp.isValid())
        return;
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Clear comp");
    clip.state.removeChild (comp, &um);
    // The ValueTree listener notices the removal and clears the source override.
}

void CompingEngine::selectWholeTake (te::WaveAudioClip& clip, int takeIndex)
{
    auto takes = clip.state.getChildWithName (te::IDs::TAKES);
    auto take = takes.getChild (takeIndex);
    if (! take.isValid())
        return;

    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Select take");

    auto comp = getCompTree (clip);
    if (comp.isValid())
        clip.state.removeChild (comp, &um);

    // TE's WaveAudioClip::setCurrentTake REMOVES a take whose source has no
    // ProjectItem (always the case for our direct-file loop takes). Switching
    // the clip's source string directly is the project-less equivalent.
    clip.state.setProperty (te::IDs::source, take.getProperty (te::IDs::source).toString(), &um);
}

//==============================================================================
// Render scheduling

void CompingEngine::scheduleRenderForClipState (juce::ValueTree clipState)
{
    const auto id = te::EditItemID::fromID (clipState);
    if (! id.isValid())
        return;
    pendingClips.addIfNotAlreadyThere (id);
    startTimer (kDebounceMs);
}

void CompingEngine::timerCallback()
{
    stopTimer();
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
    {
        pendingClips.clear();
        return;
    }

    auto clips = std::move (pendingClips);
    pendingClips.clear();

    for (const auto& id : clips)
    {
        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (*edit, id)))
        {
            if (hasComp (*wc))
                startRenderJob (*wc);
            else
                setSourceOverride (*wc, {});   // comp removed (clear/undo) → raw take
        }
    }
}

void CompingEngine::startRenderJob (te::WaveAudioClip& clip)
{
    const auto clipID    = clip.itemID;
    const auto takeFiles = getTakeFiles (clip);
    const auto sections  = getSections (clip);
    if (takeFiles.isEmpty() || sections.isEmpty())
        return;

    const auto destFile = compCacheDirectory().getChildFile (
        "comp_" + clipID.toString() + "_" + compHash (clipID, takeFiles, sections) + ".wav");

    if (destFile.existsAsFile())
    {
        applyRenderedFile (clipID, destFile);
        return;
    }

    renderPool.addJob ([this, aliveFlag = alive, clipID, takeFiles, sections, destFile]
    {
        const auto result = renderCompFile (takeFiles, sections, destFile);
        if (result == juce::File())
            return;

        juce::MessageManager::callAsync ([this, aliveFlag, clipID, result]
        {
            if (! aliveFlag->load())
                return;
            applyRenderedFile (clipID, result);
        });
    });
}

void CompingEngine::applyRenderedFile (te::EditItemID clipID, const juce::File& file)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;
    if (auto* wc = dynamic_cast<te::WaveAudioClip*> (te::findClipForID (*edit, clipID)))
        if (hasComp (*wc))   // user may have cleared the comp while rendering
            setSourceOverride (*wc, file);
}

juce::File CompingEngine::renderCompNow (te::WaveAudioClip& clip)
{
    const auto takeFiles = getTakeFiles (clip);
    const auto sections  = getSections (clip);
    if (takeFiles.isEmpty() || sections.isEmpty())
        return {};

    const auto destFile = compCacheDirectory().getChildFile (
        "comp_" + clip.itemID.toString() + "_" + compHash (clip.itemID, takeFiles, sections) + ".wav");

    const auto result = destFile.existsAsFile() ? destFile
                                                : renderCompFile (takeFiles, sections, destFile);
    if (result != juce::File())
        setSourceOverride (clip, result);
    return result;
}

//==============================================================================
juce::File CompingEngine::compCacheDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("Cloudes Comps");
    dir.createDirectory();
    return dir;
}

juce::String CompingEngine::compHash (const te::EditItemID& clipID,
                                      const juce::Array<juce::File>& takeFiles,
                                      const juce::Array<Section>& sections)
{
    juce::String s (clipID.toString());
    for (const auto& f : takeFiles)
        s << "|" << f.getFullPathName() << ":" << juce::String (f.getLastModificationTime().toMilliseconds());
    for (const auto& sec : sections)
        s << "|" << sec.takeIndex << "@" << juce::String (sec.endTime, 6);
    return juce::String::toHexString (s.hashCode64());
}

juce::File CompingEngine::renderCompFile (const juce::Array<juce::File>& takeFiles,
                                          const juce::Array<Section>& sections,
                                          const juce::File& destFile)
{
    juce::AudioFormatManager afm;
    afm.registerBasicFormats();

    // Open all resolvable take readers; output format follows the first one.
    juce::OwnedArray<juce::AudioFormatReader> readers;
    for (const auto& f : takeFiles)
        readers.add (f.existsAsFile() ? afm.createReaderFor (f) : nullptr);

    double sampleRate = 0.0;
    int numChannels = 1;
    juce::int64 totalSamples = 0;
    for (auto* r : readers)
    {
        if (r == nullptr || r->sampleRate <= 0.0)
            continue;
        if (sampleRate <= 0.0)
            sampleRate = r->sampleRate;
        numChannels  = juce::jmax (numChannels, (int) r->numChannels);
        totalSamples = juce::jmax (totalSamples, r->lengthInSamples);
    }
    if (sampleRate <= 0.0 || totalSamples <= 0)
        return {};

    numChannels = juce::jmin (numChannels, 2);
    juce::AudioBuffer<float> out (numChannels, (int) totalSamples);
    out.clear();

    const int xfade = juce::jmax (1, (int) std::lround (sampleRate * kCrossfadeSecs));
    const int n = sections.size();
    juce::int64 prevEnd = 0;

    for (int i = 0; i < n; ++i)
    {
        const auto& sec = sections.getReference (i);
        const auto secStart = prevEnd;
        const auto secEnd   = juce::jlimit<juce::int64> (secStart, totalSamples,
                                  (juce::int64) std::lround (sec.endTime * sampleRate));
        prevEnd = secEnd;

        auto* reader = juce::isPositiveAndBelow (sec.takeIndex, readers.size())
                           ? readers[sec.takeIndex] : nullptr;
        if (reader == nullptr)
            continue;   // silence section

        // Expand by half a crossfade on interior boundaries (TE does the same).
        const juce::int64 a = juce::jmax<juce::int64> (0, secStart - (i > 0 ? xfade / 2 : 0));
        const juce::int64 b = juce::jmin<juce::int64> (totalSamples, secEnd + (i < n - 1 ? xfade / 2 : 0));
        const int numToRead = (int) juce::jmax<juce::int64> (0, juce::jmin (b, reader->lengthInSamples) - a);
        if (numToRead <= 0)
            continue;

        juce::AudioBuffer<float> temp (numChannels, (int) (b - a));
        temp.clear();
        reader->read (&temp, 0, numToRead, a, true, numChannels > 1);

        // Equal-power fades at interior boundaries.
        if (i > 0)
        {
            const int fadeLen = juce::jmin (xfade, temp.getNumSamples());
            for (int ch = 0; ch < numChannels; ++ch)
                temp.applyGainRamp (ch, 0, fadeLen, 0.0f, 1.0f);
        }
        if (i < n - 1)
        {
            const int fadeLen = juce::jmin (xfade, temp.getNumSamples());
            for (int ch = 0; ch < numChannels; ++ch)
                temp.applyGainRamp (ch, temp.getNumSamples() - fadeLen, fadeLen, 1.0f, 0.0f);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            out.addFrom (ch, (int) a, temp, ch, 0, temp.getNumSamples());
    }

    destFile.getParentDirectory().createDirectory();
    auto temporary = destFile.getSiblingFile (destFile.getFileNameWithoutExtension() + ".part.wav");
    temporary.deleteFile();

    juce::WavAudioFormat wav;
    {
        auto stream = temporary.createOutputStream();
        if (stream == nullptr)
            return {};
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), sampleRate, (unsigned int) numChannels, 32, {}, 0));
        if (writer == nullptr)
            return {};
        stream.release();   // writer owns the stream now
        if (! writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
            return {};
    }

    destFile.deleteFile();
    if (! temporary.moveFileTo (destFile))
        return {};
    return destFile;
}

//==============================================================================
// ValueTree listener — any change inside a CLOUDESCOMP subtree re-renders

void CompingEngine::handleTreeChange (juce::ValueTree tree)
{
    auto t = tree;
    while (t.isValid() && ! t.hasType (compType))
        t = t.getParent();
    if (! t.isValid())
        return;
    auto clipState = t.getParent();
    if (clipState.isValid())
        scheduleRenderForClipState (clipState);
}

void CompingEngine::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    handleTreeChange (tree);
}

void CompingEngine::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (compType))
        scheduleRenderForClipState (parent);
    else
        handleTreeChange (parent);
}

void CompingEngine::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (compType))
        scheduleRenderForClipState (parent);   // comp removed → timer clears the override
    else
        handleTreeChange (parent);
}
