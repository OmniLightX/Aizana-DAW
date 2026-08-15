#include "TempoEngine.h"

using TimePos = tracktion::core::TimePosition;
using BeatPos = tracktion::core::BeatPosition;
using TimeDur = tracktion::core::TimeDuration;

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

TempoEngine::ExtractResult TempoEngine::extractTempoMap (const juce::Array<double>& beatTimes)
{
    ExtractResult result;

    auto* edit = editManager.getEdit();
    if (edit == nullptr || beatTimes.size() < 2)
        return result;

    // Aufsteigend + sinnvolle Intervalle (20..300 BPM entspricht 0.2..3 s)
    for (int i = 0; i + 1 < beatTimes.size(); ++i)
        if (beatTimes[i + 1] - beatTimes[i] < 0.005)
            return result;

    // Median-Intervall für die Vorlauf-Schätzung
    juce::Array<double> intervals;
    for (int i = 0; i + 1 < beatTimes.size(); ++i)
        intervals.add (beatTimes[i + 1] - beatTimes[i]);
    intervals.sort();
    const double medianDt = intervals[intervals.size() / 2];
    if (medianDt <= 0.005)
        return result;

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

    // ---- Vorlauf (C1) --------------------------------------------------------
    // Das Tempo vor dem ersten Beat ist das ERKANNTE Tempo — nie ein aus der
    // Vorlaufstrecke zurückgerechneter Wert. Damit das Raster trotzdem exakt
    // auf dem Audio sitzt, rückt das Audio auf die nächste TAKTLINIE (Studio
    // Ones Handarbeit „ersten Downbeat auf einen Downbeat ziehen", automatisch).
    // Grundtempo aus dem MEDIAN-Abstand, nicht aus dem ersten Intervall: ein
    // einzelnes Intervall ist bis auf einen Analyse-Frame genau (11,6 ms bei
    // Hop 512 / 44,1 kHz ≈ ±3 BPM) — das Songtempo würde bei jeder Extraktion
    // anders zittern. Die Genauigkeit am Audio geht dadurch nicht verloren:
    // ab dem ersten Beat setzt die Schleife unten pro Intervall einen eigenen
    // Step. Der Vorlauf deckt ohnehin kein Audio ab.
    const double t0 = beatTimes[0];

    double bpm0 = juce::jlimit (20.0, 300.0, 60.0 / medianDt);

    // Beat-Länge nach dem Clamp neu bestimmen: bei absurdem Material (Clamp
    // greift) müssen Raster und Verschiebung zueinander passen, sonst sitzt das
    // Audio hinterher NEBEN der Linie, die wir gerade berechnet haben.
    const double beatLen = 60.0 / bpm0;

    int barBeats = 4;
    if (auto* ts = seq.getTimeSig (0))
        barBeats = juce::jlimit (1, 32, ts->numerator.get());

    int preBeats = 0;
    if (t0 > 0.05)
    {
        const double rawBars = t0 / (beatLen * (double) barBeats);
        preBeats = juce::jmax (0, (int) std::llround (rawBars)) * barBeats;
    }

    // Nach vorn schieben, solange irgendein Clip sonst vor 0 landen würde.
    // (Rückwärts-Verschiebung ist der kürzere Weg und deshalb der Normalfall —
    // sie darf nur nie Material aus der Timeline drücken.)
    auto shiftFor = [&] (int beats) { return (double) beats * beatLen - t0; };

    double earliestStart = 0.0;
    bool haveClip = false;
    for (auto& f : frozen)
    {
        const double s = f.pos.time.getStart().inSeconds();
        if (! haveClip || s < earliestStart) { earliestStart = s; haveClip = true; }
    }

    while (haveClip && earliestStart + shiftFor (preBeats) < -1.0e-9)
        preBeats += barBeats;

    result.leadInBeats  = preBeats;
    result.leadInBpm    = bpm0;
    result.shiftSeconds = shiftFor (preBeats);

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

    // Audio zurück an seine Zeit-Positionen (beat-verankerte Clips wären
    // gewandert), plus die Vorlauf-Korrektur: alle Clips um denselben Betrag,
    // damit ihre Abstände zueinander erhalten bleiben.
    const auto shift = TimeDur::fromSeconds (result.shiftSeconds);
    for (auto& f : frozen)
    {
        auto p = f.pos;
        p.time = p.time + shift;
        f.clip->setPosition (p);
    }

    result.numPoints = numPoints;
    return result;
}

TempoEngine::ExtractResult
TempoEngine::extractTempoMapFromClip (te::WaveAudioClip& clip,
                                      const juce::Array<double>& beatTimesFileSecs)
{
    // Timestretch (autoTempo) verzerrt die Zeitbasis — Datei-Zeit wäre nicht
    // mehr 1:1 Edit-Zeit. Erst Anpassung entfernen, dann extrahieren.
    if (clip.getAutoTempo())
        return {};

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
