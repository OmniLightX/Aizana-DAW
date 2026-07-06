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
