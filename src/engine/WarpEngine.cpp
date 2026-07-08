#include "WarpEngine.h"

namespace
{
    // Warp braucht einen prozessierbaren Timestretch-Modus. Elastique ist im
    // Build nicht enthalten — SoundTouch (bessere Qualität) wie in
    // ClipEditor::conformToSongTempo.
    void ensureStretchMode (te::WaveAudioClip& clip)
    {
        if (! te::TimeStretcher::canProcessFor (clip.getTimeStretchMode()))
            clip.setTimeStretchMode (te::TimeStretcher::soundtouchBetter);
    }

    /**
     * Ersatz für TEs WarpTimeRenderJob mit EINEM Unterschied: die
     * Segment-Liste wird aus der ORIGINALDATEI gebaut (Overload
     * AudioSegmentList::create(clip, wtm, sourceFile)) statt über
     * clip.getWaveInfo(). Letzteres liefert während needsRender() die noch
     * nicht existierende Zieldatei (sampleRate 0) → leere Segmente → TEs Job
     * rendert STILLE (real gefangen; TEs Fallback greift nur mit
     * Project-Items, Aizana nutzt direkte File-Referenzen, I-07).
     *
     * Registriert sich im RenderManager auf denselben Ziel-Hash wie TEs Job —
     * clip.renderSource() adoptiert ihn über getRenderJobWithoutCreating().
     * Render-Logik ansonsten 1:1 wie tracktion_WarpTimeRenderJob.h.
     */
    struct AizanaWarpRenderJob : public te::RenderManager::Job
    {
        static Ptr getOrCreate (te::WaveAudioClip& clip, const te::AudioFile& target)
        {
            if (auto ptr = clip.edit.engine.getRenderManager().getRenderJobWithoutCreating (target))
                return ptr;

            return *new AizanaWarpRenderJob (clip, target);
        }

    protected:
        bool setUpRender() override    { return true; }
        bool completeRender() override { return true; }

        bool renderNextBlock() override
        {
            if (proxyInfo != nullptr)
            {
                te::AudioFileInfo sourceInfo (source.getInfo());

                // AIFF-Metadaten können nicht in WAV geschrieben werden
                if (sourceInfo.metadata.getValue ("MetaDataSource", "None") == "AIFF")
                    sourceInfo.metadata.clear();

                te::AudioFileWriter writer (proxy, engine.getAudioFileFormatManager().getWavFormat(),
                                            sourceInfo.numChannels, sourceInfo.sampleRate,
                                            juce::jmax (16, sourceInfo.bitsPerSample),
                                            sourceInfo.metadata, 0);

                ok = writer.isOpen()
                      && proxyInfo->render (engine, source, writer, this, progress);
            }

            // Fehlschlag: Original kopieren statt Endlos-Retry (wie TE)
            if (! shouldExit() && ! ok)
                source.getFile().copyFileTo (proxy.getFile());

            return true;
        }

    private:
        AizanaWarpRenderJob (te::WaveAudioClip& clip, const te::AudioFile& target)
            : Job (clip.edit.engine, target),
              source (clip.edit.engine, clip.getOriginalFile())
        {
            const auto tm = clip.getTimeStretchMode();

            proxyInfo = std::make_unique<te::AudioClipBase::ProxyRenderingInfo>();
            proxyInfo->audioSegmentList = te::AudioSegmentList::create (clip, clip.getWarpTimeManager(), source);
            proxyInfo->clipTime         = { {}, clip.getWarpTimeManager().getWarpEndMarkerTime() };
            proxyInfo->speedRatio       = 1.0;
            proxyInfo->mode             = (tm != te::TimeStretcher::disabled && tm != te::TimeStretcher::melodyne)
                                            ? tm : te::TimeStretcher::defaultMode;
        }

        te::AudioFile source;
        bool ok = false;
        std::unique_ptr<te::AudioClipBase::ProxyRenderingInfo> proxyInfo;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AizanaWarpRenderJob)
    };
}

bool WarpEngine::isWarped (const te::WaveAudioClip& clip)
{
    return const_cast<te::WaveAudioClip&> (clip).getWarpTime();
}

void WarpEngine::setWarpEnabled (te::WaveAudioClip& clip, bool enabled)
{
    if (enabled)
    {
        // Der WarpTimeRenderJob stretcht mit dem Clip-Modus — muss prozessierbar sein.
        ensureStretchMode (clip);
        clip.setWarpTime (true);

        // Manager anfassen: seedet beim ersten Mal die Endpunkt-Marker
        // (0 und Quelllänge) und startet den Transient-Detection-Job.
        clip.getWarpTimeManager();
    }
    else
    {
        // TE stellt die Original-Quelle wieder her (setWarpTime(false)
        // → updateSourceFile/getOriginalFile im Property-Listener).
        clip.setWarpTime (false);
    }
}

juce::Array<WarpEngine::Marker> WarpEngine::getMarkers (const te::WaveAudioClip& clip)
{
    juce::Array<Marker> out;

    if (! isWarped (clip))
        return out;

    auto& wtm = const_cast<te::WaveAudioClip&> (clip).getWarpTimeManager();
    for (auto* m : wtm.getMarkers())
        out.add ({ m->sourceTime.inSeconds(), m->warpTime.inSeconds() });

    return out;
}

int WarpEngine::addMarkerAtSourceTime (te::WaveAudioClip& clip, double sourceSecs)
{
    if (! isWarped (clip))
        return -1;

    auto& wtm = clip.getWarpTimeManager();

    const auto src  = tracktion::TimePosition::fromSeconds (sourceSecs);
    const auto warp = wtm.sourceTimeToWarpTime (src);

    return wtm.insertMarker (te::WarpMarker (src, warp));
}

double WarpEngine::moveMarker (te::WaveAudioClip& clip, int index, double newWarpSecs)
{
    if (! isWarped (clip))
        return newWarpSecs;

    return clip.getWarpTimeManager()
        .moveMarker (index, tracktion::TimePosition::fromSeconds (newWarpSecs))
        .inSeconds();
}

void WarpEngine::removeMarker (te::WaveAudioClip& clip, int index)
{
    if (isWarped (clip))
        clip.getWarpTimeManager().removeMarker (index);
}

void WarpEngine::resetMarkers (te::WaveAudioClip& clip)
{
    if (isWarped (clip))
        clip.getWarpTimeManager().removeAllMarkers();
}

double WarpEngine::warpToSource (const te::WaveAudioClip& clip, double warpSecs)
{
    if (! isWarped (clip))
        return warpSecs;

    return const_cast<te::WaveAudioClip&> (clip).getWarpTimeManager()
        .warpTimeToSourceTime (tracktion::TimePosition::fromSeconds (warpSecs)).inSeconds();
}

double WarpEngine::sourceToWarp (const te::WaveAudioClip& clip, double sourceSecs)
{
    if (! isWarped (clip))
        return sourceSecs;

    return const_cast<te::WaveAudioClip&> (clip).getWarpTimeManager()
        .sourceTimeToWarpTime (tracktion::TimePosition::fromSeconds (sourceSecs)).inSeconds();
}

std::pair<bool, juce::Array<double>> WarpEngine::getTransients (const te::WaveAudioClip& clip)
{
    if (! isWarped (clip))
        return { false, {} };

    auto [ready, times] = const_cast<te::WaveAudioClip&> (clip)
                              .getWarpTimeManager().getTransientTimes();

    juce::Array<double> out;
    for (const auto& t : times)
        out.add (t.inSeconds());

    return { ready, std::move (out) };
}

void WarpEngine::kickWarpRender (te::WaveAudioClip& clip)
{
    if (! isWarped (clip))
        return;

    auto& edit = clip.edit;

    // Quelle in den AudioFileCache aufwärmen, BEVOR der Render-BG-Thread sie
    // liest. Ohne das kann der StretchSegment-Reader auf einen noch nicht
    // befüllten Cache-Eintrag treffen und Nullen liefern → stiller Warp-Render
    // (nur unter Last reproduzierbar, z. B. viele Edits kurz nacheinander).
    // Ein einmaliger Sample-Read auf dem Message-Thread garantiert einen warmen
    // Cache. Der AudioFileCache lebt pro Engine; derselbe, den der Job nutzt.
    {
        te::AudioFile origin (edit.engine, clip.getOriginalFile());
        if (auto reader = edit.engine.getAudioFileManager().cache.createReader (origin))
        {
            juce::AudioBuffer<float> warm (juce::jmax (1, origin.getInfo().numChannels), 4096);
            reader->setReadPosition (0);
            reader->readSamples (warm.getNumSamples(), warm, warm.getNumChannels() == 1
                                     ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(),
                                 0, juce::AudioChannelSet::canonicalChannelSet (warm.getNumChannels()),
                                 5000);
        }
    }

    const auto target = te::RenderManager::getAudioFileForHash (
        edit.engine, edit.getTempDirectory (false), clip.getHash());

    // Job VOR TEs 600ms-Proxy-Timer registrieren — renderSource() adoptiert
    // dann unseren (korrekten) Job statt einen eigenen (stillen) zu bauen.
    te::RenderManager::Job::Ptr job;
    if (! (target.getFile().existsAsFile() || target.isValid()))
        job = AizanaWarpRenderJob::getOrCreate (clip, target);

    // Quelle auf die Zieldatei umstellen + Job/fertige Datei adoptieren.
    clip.updateSourceFile();
}

bool WarpEngine::isWarpRenderPending (const te::WaveAudioClip& clip)
{
    if (! isWarped (clip))
        return false;

    auto& c = const_cast<te::WaveAudioClip&> (clip);
    const auto target = te::RenderManager::getAudioFileForHash (
        c.edit.engine, c.edit.getTempDirectory (false), c.getHash());

    // Läuft/queued noch ein Render-Job für den aktuellen Hash? → pending.
    if (c.edit.engine.getRenderManager().getRenderJobWithoutCreating (target) != nullptr)
        return true;

    // Kein Job → pending, solange der Clip noch nicht auf die Zieldatei
    // umgestellt ist (der deferred WarpRenderWatcher-Kick lief noch nicht).
    // Bewusst KEIN AudioFile::isValid() hier: das liest den AudioFileCache, der
    // während/nach dem Render veraltete Info liefern kann — die Job-Existenz ist
    // das autoritative „läuft noch"-Signal.
    return c.getAudioFile().getFile() != target.getFile();
}

//==============================================================================
// WarpRenderWatcher

void WarpRenderWatcher::attach (te::Edit& e)
{
    detach();
    edit = &e;
    aliveToken = std::make_shared<int> (0);
    e.state.addListener (this);
}

void WarpRenderWatcher::detach()
{
    if (edit != nullptr)
        edit->state.removeListener (this);

    edit = nullptr;
    aliveToken.reset();   // ausstehende callAsyncs verfallen (I-11)
}

void WarpRenderWatcher::scheduleKick (juce::ValueTree treeInsideClip)
{
    if (edit == nullptr)
        return;

    // Zum Clip-State hochlaufen (WARPMARKER → WARPMARKERS → WARPTIME → AUDIOCLIP)
    auto clipState = treeInsideClip;
    while (clipState.isValid() && ! clipState.hasType (te::IDs::AUDIOCLIP))
        clipState = clipState.getParent();

    if (! clipState.isValid())
        return;

    // Async: nie innerhalb des VT-/Undo-Callbacks mutieren; der Kick läuft
    // trotzdem lange vor TEs 600ms-Timer. ValueTree ist ref-counted (safe),
    // der Edit-Zeiger wird über das aliveToken gegen den Swap geschützt.
    juce::MessageManager::callAsync (
        [weak = std::weak_ptr<int> (aliveToken), ed = edit, clipState]
        {
            if (weak.expired())
                return;

            for (auto* t : te::getAudioTracks (*ed))
                for (auto* c : t->getClips())
                    if (c->state == clipState)
                        if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                            WarpEngine::kickWarpRender (*wc);
        });
}

void WarpRenderWatcher::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    // Marker verschoben ODER Bend am Clip an-/abgeschaltet (Undo inklusive)
    if (tree.hasType (te::IDs::WARPMARKER) || id == te::IDs::warpTime)
        scheduleKick (tree);
}

void WarpRenderWatcher::valueTreeChildAdded (juce::ValueTree& /*parent*/, juce::ValueTree& child)
{
    if (child.hasType (te::IDs::WARPMARKER))
        scheduleKick (child);
}

void WarpRenderWatcher::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (te::IDs::WARPMARKER))
        scheduleKick (parent);   // child ist schon entkoppelt — über den Parent auflösen
}
