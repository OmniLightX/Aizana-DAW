#include "VocalTuner.h"

#include <cmath>
#include <vector>

//==============================================================================
// Reine Kerne

double VocalTuner::hzToMidi (double hz)
{
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

double VocalTuner::midiToHz (double midi)
{
    return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
}

int VocalTuner::snapMidiToScale (double midi, int keyRootNote, Scale scale)
{
    if (scale == Scale::chromatic)
        return juce::roundToInt (midi);

    static const int majorDegrees[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorDegrees[] = { 0, 2, 3, 5, 7, 8, 10 };   // natürlich Moll
    const int* degrees = (scale == Scale::major) ? majorDegrees : minorDegrees;

    // Kandidaten in ±1 Oktave um den Ton durchsuchen, nächstliegenden nehmen
    // (Gleichstand → abwärts: der tiefere Ton gewinnt, klingt natürlicher).
    const int baseOctave = (int) std::floor (midi / 12.0);
    int    best     = juce::roundToInt (midi);
    double bestDist = 1.0e9;

    for (int oct = baseOctave - 1; oct <= baseOctave + 1; ++oct)
    {
        for (int i = 0; i < 7; ++i)
        {
            const int candidate = oct * 12 + (keyRootNote + degrees[i]) % 12;
            const double dist = std::abs (midi - candidate);
            if (dist < bestDist - 1.0e-9
                || (std::abs (dist - bestDist) < 1.0e-9 && candidate < best))
            {
                bestDist = dist;
                best = candidate;
            }
        }
    }

    return best;
}

juce::Array<float> VocalTuner::buildCorrectionSemitones (const juce::Array<float>& pitchHz,
                                                         const Settings& settings,
                                                         double frameRate)
{
    juce::Array<float> corr;
    corr.ensureStorageAllocated (pitchHz.size());

    // Roh-Korrektur: voiced → (Skalenton − Ist) · Stärke; unvoiced → letzte halten
    float last = 0.0f;
    for (float hz : pitchHz)
    {
        if (hz > 0.0f)
        {
            const double midi   = hzToMidi (hz);
            const int    target = snapMidiToScale (midi, settings.keyRootNote, settings.scale);
            last = (float) ((target - midi) * settings.strength);
        }
        corr.add (last);
    }

    // One-Pole-Glättung („Speed“) — verhindert harte Zips zwischen Noten
    if (settings.smoothingMs > 0.0f && frameRate > 0.0 && corr.size() > 1)
    {
        const float alpha = std::exp ((float) (-1.0 / (settings.smoothingMs * 0.001 * frameRate)));
        float state = corr[0];
        for (int i = 0; i < corr.size(); ++i)
        {
            state = alpha * state + (1.0f - alpha) * corr[i];
            corr.set (i, state);
        }
    }

    return corr;
}

bool VocalTuner::renderCorrected (const juce::AudioBuffer<float>& in,
                                  juce::AudioBuffer<float>& out,
                                  double sampleRate,
                                  const juce::Array<float>& correctionSemitones,
                                  int hopSize)
{
    const int numChannels = in.getNumChannels();
    const int numSamples  = in.getNumSamples();
    if (numChannels < 1 || numSamples < 1 || sampleRate <= 0.0 || hopSize < 1)
        return false;

    te::TimeStretcher ts;
    ts.initialise (sampleRate, 512, numChannels, te::TimeStretcher::soundtouchBetter, {}, false);
    if (! ts.isInitialised())
        return false;

    out.setSize (numChannels, numSamples);
    out.clear();

    juce::AudioBuffer<float> feed (numChannels, 8192);      // Input-Häppchen (ggf. 0-gepolstert)
    juce::AudioBuffer<float> chunk (numChannels, 512);      // Output-Block des Stretchers

    std::vector<const float*> ins ((size_t) numChannels);
    std::vector<float*>       outs ((size_t) numChannels);
    for (int c = 0; c < numChannels; ++c)
        outs[(size_t) c] = chunk.getWritePointer (c);

    int inPos = 0, outPos = 0;
    bool flushing = false;
    int guard = 0;
    const int maxIterations = 8 * (numSamples / 512 + 64);   // hartes Abbruch-Budget

    while (outPos < numSamples && ++guard < maxIterations)
    {
        int produced = 0;

        if (! flushing)
        {
            const int needed = ts.getFramesNeeded();

            if (needed > 0)
            {
                if (needed > feed.getNumSamples())
                    feed.setSize (numChannels, needed, false, false, true);
                feed.clear();

                const int give = juce::jmin (needed, numSamples - inPos);
                for (int c = 0; c < numChannels; ++c)
                {
                    if (give > 0)
                        feed.copyFrom (c, 0, in, c, inPos, give);
                    ins[(size_t) c] = feed.getReadPointer (c);
                }

                // Pitch-Shift dem Input-Fortschritt entlang nachführen
                const int frame = juce::jlimit (0, juce::jmax (0, correctionSemitones.size() - 1),
                                                inPos / hopSize);
                ts.setSpeedAndPitch (1.0f, correctionSemitones.isEmpty() ? 0.0f
                                                                         : correctionSemitones[frame]);

                produced = ts.processData (ins.data(), needed, outs.data());
                inPos += needed;

                if (inPos >= numSamples)
                    flushing = true;
            }
            else
            {
                produced = ts.processData (ins.data(), 0, outs.data());
                if (produced <= 0)
                    flushing = true;
            }
        }
        else
        {
            produced = ts.flush (outs.data());
            if (produced <= 0)
                break;
        }

        const int take = juce::jmin (produced, numSamples - outPos);
        for (int c = 0; c < numChannels && take > 0; ++c)
            out.copyFrom (c, outPos, chunk, c, 0, take);
        outPos += juce::jmax (0, take);
    }

    return outPos > 0;
}

//==============================================================================
// Clip-Workflow

juce::String VocalTuner::Analysis::describe() const
{
    switch (status)
    {
        case Status::stretched:
            return juce::String::fromUTF8 (
                "Der Clip ist zeitgedehnt (Songtempo-Anpassung oder Warp). Vocal-Tuning "
                "arbeitet auf der Originaldatei \xe2\x80\x94 erst die Anpassung entfernen.");
        case Status::unreadableSource:
            return juce::String::fromUTF8 (
                "Die Quelldatei des Clips ist nicht lesbar (fehlt, verschoben, oder eine "
                "Warp-Datei ist noch nicht fertig gerendert).");
        case Status::tooShort:
            return juce::String::fromUTF8 (
                "Der gespielte Ausschnitt ist zu kurz f\xc3\xbcr eine Tonh\xc3\xb6henanalyse "
                "(unter 0,1 Sekunden).");
        case Status::notTonal:
            return juce::String::fromUTF8 (
                "In diesem Clip ist kein einziger Ton mit erkennbarer Tonh\xc3\xb6he \xe2\x80\x94 "
                "reines Schlagzeug, Rauschen oder Stille l\xc3\xa4sst sich nicht stimmen.");
        case Status::renderFailed:
            return juce::String::fromUTF8 ("Die Tonh\xc3\xb6hen-Verschiebung konnte nicht "
                                           "gestartet werden.");
        case Status::writeFailed:
            return juce::String::fromUTF8 ("Die getunte Datei konnte nicht geschrieben werden.");
        case Status::emptyClip:
            return juce::String::fromUTF8 ("Der Clip hat keine L\xc3\xa4nge.");
        case Status::noTrack:
        case Status::wrongEdit:
            return juce::String::fromUTF8 ("Der Clip geh\xc3\xb6rt zu keiner Spur dieser Session.");
        case Status::ok:
            break;
    }

    juce::String s;
    s << juce::String::fromUTF8 ("Erkannt: Grundton ") << juce::String (medianHz, 1) << " Hz, "
      << juce::String (100.0 * voicedRatio, 0)
      << juce::String::fromUTF8 (" % des Ausschnitts haben eine klare Tonh\xc3\xb6he.");

    if (voicedRatio < kDoubtfulVoicedRatio)
        s << juce::String::fromUTF8 ("\nDas ist wenig \xe2\x80\x94 typisch f\xc3\xbcr Akkorde, "
                                     "mehrere Stimmen gleichzeitig oder viel Hall. Das Ergebnis "
                                     "wird vermutlich nichts taugen; tunen kannst du es trotzdem.");

    return s;
}

VocalTuner::Prepared VocalTuner::prepare (te::WaveAudioClip& clip) const
{
    Prepared p;

    auto* edit = editManager.getEdit();
    if (edit == nullptr || &clip.edit != edit)
    {
        p.analysis.status = Status::wrongEdit;
        return p;
    }

    if (dynamic_cast<te::AudioTrack*> (clip.getClipTrack()) == nullptr)
    {
        p.analysis.status = Status::noTrack;
        return p;
    }

    // v1: nur ungestretchte Clips (Datei-Zeit == Edit-Zeit)
    if (std::abs (clip.getSpeedRatio() - 1.0) > 0.001 || clip.getAutoTempo())
    {
        p.analysis.status = Status::stretched;
        return p;
    }

    const auto range = clip.getPosition().time;
    p.analysis.lengthSecs = range.getLength().inSeconds();
    if (p.analysis.lengthSecs <= 0.0)
    {
        p.analysis.status = Status::emptyClip;
        return p;
    }

    // Aktuelle Quelle lesen (bei aktivem Warp = die gewarpte Datei) —
    // genau den Ausschnitt, den der Clip spielt (offset..offset+len).
    const auto sourceFile = clip.getAudioFile().getFile();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0)
    {
        p.analysis.status = Status::unreadableSource;
        return p;
    }

    p.rate = reader->sampleRate;
    const auto startSmp = (juce::int64) (clip.getPosition().getOffset().inSeconds() * p.rate);
    const auto lenSmp   = juce::jmin ((juce::int64) (range.getLength().inSeconds() * p.rate),
                                      reader->lengthInSamples - startSmp);
    if (lenSmp < 4096)
    {
        p.analysis.status = Status::tooShort;
        return p;
    }

    p.buffer.setSize ((int) reader->numChannels, (int) lenSmp);
    reader->read (&p.buffer, 0, (int) lenSmp, startSmp, true, true);

    // Mono-Mix für die Analyse
    juce::AudioBuffer<float> mono (1, (int) lenSmp);
    mono.clear();
    for (int c = 0; c < p.buffer.getNumChannels(); ++c)
        mono.addFrom (0, 0, p.buffer, c, 0, (int) lenSmp, 1.0f / (float) p.buffer.getNumChannels());

    PitchDetector::Settings ps;
    p.hopSize = ps.hopSize;
    p.pitchHz = PitchDetector::detectPitchHz (mono.getReadPointer (0), (int) lenSmp, p.rate, ps);

    int voiced = 0;
    for (float v : p.pitchHz)
        if (v > 0.0f)
            ++voiced;

    p.analysis.voicedRatio = p.pitchHz.isEmpty()
                               ? 0.0 : (double) voiced / (double) p.pitchHz.size();
    p.analysis.medianHz    = (double) PitchDetector::medianPitchHz (p.pitchHz);

    // Hart abgelehnt wird NUR, was gar keine Tonhöhe hat. Wenig tonales
    // Material ist ein Hinweis (kDoubtfulVoicedRatio), kein Verbot — genau der
    // Punkt, an dem die alte Fassung zu streng wirkte, ohne es zu sein.
    p.analysis.status = (p.pitchHz.isEmpty() || p.analysis.medianHz <= 0.0)
                          ? Status::notTonal : Status::ok;
    return p;
}

VocalTuner::Analysis VocalTuner::analyseClip (te::WaveAudioClip& clip) const
{
    return prepare (clip).analysis;
}

te::WaveAudioClip* VocalTuner::tuneClipInPlace (te::WaveAudioClip& clip, const Settings& settings,
                                                Analysis* outAnalysis)
{
    auto prep = prepare (clip);

    auto fail = [&prep, outAnalysis] (Status s) -> te::WaveAudioClip*
    {
        prep.analysis.status = s;
        if (outAnalysis != nullptr) *outAnalysis = prep.analysis;
        return nullptr;
    };

    if (outAnalysis != nullptr)
        *outAnalysis = prep.analysis;

    if (! prep.analysis.isTunable())
        return nullptr;

    auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
    if (track == nullptr)
        return fail (Status::noTrack);

    const auto range  = clip.getPosition().time;
    const double rate = prep.rate;
    auto& buffer      = prep.buffer;

    const auto corr = buildCorrectionSemitones (prep.pitchHz, settings, rate / prep.hopSize);

    juce::AudioBuffer<float> tuned;
    if (! renderCorrected (buffer, tuned, rate, corr, prep.hopSize))
        return fail (Status::renderFailed);

    // Getunte Datei neben die Session schreiben (Bounce-Konvention)
    const auto sessionFile = editManager.getCurrentFile();
    auto outDir = sessionFile.existsAsFile()
        ? sessionFile.getParentDirectory().getChildFile ("Bounces")
        : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AizanaBounces");
    outDir.createDirectory();

    const auto outFile = outDir
        .getChildFile (juce::File::createLegalFileName (clip.getName()) + " Tuned.wav")
        .getNonexistentSibling();

    {
        juce::WavAudioFormat wav;
        auto stream = outFile.createOutputStream();
        if (stream == nullptr)
            return fail (Status::writeFailed);
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, (unsigned int) tuned.getNumChannels(), 24, {}, 0));
        if (writer == nullptr)
            return fail (Status::writeFailed);
        stream.release();
        if (! writer->writeFromAudioSampleBuffer (tuned, 0, tuned.getNumSamples()))
            return fail (Status::writeFailed);
    }

    // Swap wie Bounce-in-Place: EIN Undo-Schritt, Position bleibt identisch.
    te::Clip::Ptr keepAlive (&clip);
    auto& um = editManager.getUndoManager();
    um.beginNewTransaction ("Vocal-Tuning");

    te::ClipPosition pos { range, {} };
    auto* newClipBase = track->insertNewClip (te::TrackItem::Type::wave,
                                              clip.getName() + " (Tuned)",
                                              pos, nullptr);
    auto* newClip = dynamic_cast<te::WaveAudioClip*> (newClipBase);
    if (newClip == nullptr)
        return fail (Status::writeFailed);

    newClip->getSourceFileReference().setToDirectFileReference (outFile, false);

    juce::ValueTree parent = keepAlive->state.getParent();
    if (parent.isValid())
        parent.removeChild (keepAlive->state, &um);

    return newClip;
}
