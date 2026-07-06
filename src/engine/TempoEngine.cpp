#include "TempoEngine.h"

using TimePos = tracktion::core::TimePosition;
using BeatPos = tracktion::core::BeatPosition;

TempoEngine::TempoEngine (EditManager& em)
    : editManager (em)
{
}

//==============================================================================
// Queries
//==============================================================================

juce::Array<TempoEngine::TempoPoint> TempoEngine::getTempoPoints() const
{
    juce::Array<TempoPoint> out;
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return out;

    auto& seq = edit->tempoSequence;
    const auto& tempos = seq.getTempos();

    for (int i = 0; i < tempos.size(); ++i)
    {
        auto* t = tempos[i];
        TempoPoint p;
        p.index   = i;
        p.beat    = t->getStartBeat().inBeats();
        p.timeSec = seq.toTime (t->getStartBeat()).inSeconds();
        p.bpm     = t->getBpm();
        out.add (p);
    }
    return out;
}

juce::Array<TempoEngine::TimeSigPoint> TempoEngine::getTimeSigPoints() const
{
    juce::Array<TimeSigPoint> out;
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return out;

    auto& seq = edit->tempoSequence;
    for (int i = 0; i < seq.getNumTimeSigs(); ++i)
    {
        auto* ts = seq.getTimeSig (i);
        TimeSigPoint p;
        p.index       = i;
        p.beat        = ts->startBeatNumber.get().inBeats();
        p.timeSec     = seq.toTime (ts->startBeatNumber.get()).inSeconds();
        p.numerator   = ts->numerator.get();
        p.denominator = ts->denominator.get();
        out.add (p);
    }
    return out;
}

//==============================================================================
// Gesten
//==============================================================================

int TempoEngine::addTempoChange (double timeSec, double bpm)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return -1;

    auto& seq = edit->tempoSequence;

    // Auf ganzen Beat runden — Tempowechsel mitten im Beat sind praktisch nie
    // gewollt und machen das spätere Greifen/Editieren unnötig fummelig.
    const double rawBeat = seq.toBeats (TimePos::fromSeconds (juce::jmax (0.0, timeSec))).inBeats();
    const auto   beat    = BeatPos::fromBeats (std::round (rawBeat));

    const double effectiveBpm = bpm > 0.0 ? juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm)
                                          : seq.getTempoAt (beat).getBpm();

    edit->getUndoManager().beginNewTransaction ("Tempowechsel einf\xc3\xbcgen");
    auto setting = seq.insertTempo (beat, effectiveBpm, 0.0f);
    if (setting == nullptr)
        return -1;

    return seq.getTempos().indexOf (setting.get());
}

void TempoEngine::setTempoBpm (int index, double bpm, bool beginTransaction)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto* t = edit->tempoSequence.getTempo (index);
    if (t == nullptr)
        return;

    if (beginTransaction)
        edit->getUndoManager().beginNewTransaction ("Tempo \xc3\xa4ndern");

    t->setBpm (juce::jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, bpm));
}

void TempoEngine::removeTempoChange (int index)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr || index <= 0)   // Punkt 0 = Song-Grundtempo, nicht löschbar
        return;

    if (index >= edit->tempoSequence.getNumTempos())
        return;

    edit->getUndoManager().beginNewTransaction ("Tempowechsel entfernen");
    edit->tempoSequence.removeTempo (index, true);   // remapEdit: Clips halten Beats
}

int TempoEngine::addTimeSigChange (double timeSec, int numerator, int denominator)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return -1;

    auto& seq = edit->tempoSequence;

    edit->getUndoManager().beginNewTransaction ("Taktartwechsel einf\xc3\xbcgen");
    auto setting = seq.insertTimeSig (TimePos::fromSeconds (juce::jmax (0.0, timeSec)));
    if (setting == nullptr)
        return -1;

    setting->numerator   = juce::jlimit (1, 32, numerator);
    setting->denominator = juce::jlimit (1, 32, denominator);

    for (int i = 0; i < seq.getNumTimeSigs(); ++i)
        if (seq.getTimeSig (i) == setting.get())
            return i;
    return -1;
}

void TempoEngine::setTimeSig (int index, int numerator, int denominator)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr)
        return;

    auto* ts = edit->tempoSequence.getTimeSig (index);
    if (ts == nullptr)
        return;

    edit->getUndoManager().beginNewTransaction ("Taktart \xc3\xa4ndern");
    ts->numerator   = juce::jlimit (1, 32, numerator);
    ts->denominator = juce::jlimit (1, 32, denominator);
}

void TempoEngine::removeTimeSigChange (int index)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr || index <= 0)
        return;

    if (index >= edit->tempoSequence.getNumTimeSigs())
        return;

    edit->getUndoManager().beginNewTransaction ("Taktartwechsel entfernen");
    edit->tempoSequence.removeTimeSig (index);
}

//==============================================================================
// P7a: Tempo-Map-Extraktion
//==============================================================================

int TempoEngine::extractTempoMap (const juce::Array<double>& beatTimes)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr || beatTimes.size() < 2)
        return 0;

    // Aufsteigend + sinnvolle Intervalle (20..300 BPM entspricht 0.2..3 s)
    for (int i = 0; i + 1 < beatTimes.size(); ++i)
        if (beatTimes[i + 1] - beatTimes[i] < 0.005)
            return 0;

    // Median-Intervall für die Vorlauf-Schätzung
    juce::Array<double> intervals;
    for (int i = 0; i + 1 < beatTimes.size(); ++i)
        intervals.add (beatTimes[i + 1] - beatTimes[i]);
    intervals.sort();
    const double medianDt = intervals[intervals.size() / 2];
    if (medianDt <= 0.005)
        return 0;

    auto& seq = edit->tempoSequence;
    edit->getUndoManager().beginNewTransaction ("Tempo-Map aus Clip");

    // TE verankert Clip-Positionen an BEATS — jede Tempo-Änderung würde das
    // Audio verschieben (real im Test gefangen: der importierte Clip wanderte
    // von 2.0 s auf den ersten Beat). Für die Extraktion gilt das Gegenteil:
    // das AUDIO steht, nur das RASTER bewegt sich. Deshalb Zeit-Snapshot aller
    // nicht gestretchten Wave-Clips vorher, Restore nachher — in derselben
    // Transaktion (1 Undo deckt beides).
    struct FrozenClip { te::WaveAudioClip* clip; te::ClipPosition pos; };
    juce::Array<FrozenClip> frozen;
    for (auto* t : te::getAudioTracks (*edit))
        for (auto* c : t->getClips())
            if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                if (! wc->getAutoTempo())
                    frozen.add ({ wc, wc->getPosition() });

    // Bestehende Map räumen (remapEdit=false — der Restore unten übernimmt die Zeittreue)
    for (int i = seq.getNumTempos() - 1; i >= 1; --i)
        seq.removeTempo (i, false);

    // Vorlauf: erster Beat bei t0 > 0 → eine GANZE Beat-Anzahl endet exakt dort
    const double t0 = beatTimes[0];
    int preBeats = 0;
    double bpm0 = 0.0;

    if (t0 > 0.05)
    {
        preBeats = juce::jmax (1, (int) std::round (t0 / medianDt));
        bpm0     = 60.0 * (double) preBeats / t0;
    }
    else
    {
        bpm0 = 60.0 / (beatTimes[1] - beatTimes[0]);
    }

    bpm0 = juce::jlimit (20.0, 300.0, bpm0);

    if (auto first = seq.getTempo (0))
    {
        first->setBpm (bpm0);
        first->setCurve (1.0f);   // Step: BPM gilt konstant bis zum nächsten Punkt
    }

    // Pro Beat-Intervall ein Step-Tempo an ganzzahliger Beat-Position; Läufe
    // mit (nahezu) gleicher BPM fallen zu einem Punkt zusammen.
    double lastBpm = bpm0;
    int numPoints = 1;

    for (int i = 0; i + 1 < beatTimes.size(); ++i)
    {
        const double dt  = beatTimes[i + 1] - beatTimes[i];
        const double bpm = juce::jlimit (20.0, 300.0, 60.0 / dt);

        if (std::abs (bpm - lastBpm) < 0.02)
            continue;

        seq.insertTempo (BeatPos::fromBeats ((double) (preBeats + i)), bpm, 1.0f);
        lastBpm = bpm;
        ++numPoints;
    }

    // Audio zurück an seine Zeit-Positionen (beat-verankerte Clips wären gewandert)
    for (auto& f : frozen)
        f.clip->setPosition (f.pos);

    return numPoints;
}

int TempoEngine::extractTempoMapFromClip (te::WaveAudioClip& clip,
                                          const juce::Array<double>& beatTimesFileSecs)
{
    // Timestretch (autoTempo) verzerrt die Zeitbasis — Datei-Zeit wäre nicht
    // mehr 1:1 Edit-Zeit. Erst Anpassung entfernen, dann extrahieren.
    if (clip.getAutoTempo())
        return 0;

    const auto pos = clip.getPosition();
    const double clipStart = pos.getStart().inSeconds();
    const double clipEnd   = pos.getEnd().inSeconds();
    const double offset    = pos.getOffset().inSeconds();

    juce::Array<double> editBeats;
    for (double t : beatTimesFileSecs)
    {
        const double editTime = clipStart + (t - offset);
        if (editTime >= clipStart - 1.0e-6 && editTime <= clipEnd + 1.0e-6
            && editTime >= 0.0)
            editBeats.add (editTime);
    }

    return extractTempoMap (editBeats);
}
