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

namespace
{
    // Rendert EINEN Abschnitt mit KONSTANTER Tonhoehe.
    //
    // Warum konstant: SoundTouch rechnet bei jedem setSpeedAndPitch seine Rate
    // und Tempo-Parameter neu und setzt dabei interne Puffer zurueck. Die alte
    // Fassung fuehrte die Tonhoehe pro Hop nach (86x pro Sekunde) — das ergab
    // eine Stoerung im Millisekundentakt. Der User hoerte "durchgehend Knacken
    // und Verzerrung", waehrend derselbe Weg mit KONSTANTER Korrektur (auch
    // +1 Halbton, also mehr als die echte Kurve je verlangt) sauber klang.
    // Genau dieser Vergleich hat die Ursache festgenagelt (2026-08-11).
    //
    // srcStart/srcLen beziehen sich auf `in`; das Ergebnis liegt in `dest` und
    // ist auf srcStart ausgerichtet. Gibt die Zahl gueltiger Samples zurueck.
    int renderConstantPitch (const juce::AudioBuffer<float>& in,
                             int srcStart, int srcLen,
                             float semitones, double sampleRate,
                             juce::AudioBuffer<float>& dest)
    {
        const int numChannels = in.getNumChannels();
        if (srcLen <= 0 || numChannels < 1)
            return 0;

        te::TimeStretcher ts;
        ts.initialise (sampleRate, 512, numChannels, te::TimeStretcher::soundtouchBetter, {}, false);
        if (! ts.isInitialised())
            return 0;

        ts.setSpeedAndPitch (1.0f, semitones);      // EINMAL, danach nie wieder

        dest.setSize (numChannels, srcLen, false, false, true);
        dest.clear();

        juce::AudioBuffer<float> feed (numChannels, 8192);
        juce::AudioBuffer<float> chunk (numChannels, 512);

        std::vector<const float*> ins ((size_t) numChannels);
        std::vector<float*>       outs ((size_t) numChannels);
        for (int c = 0; c < numChannels; ++c)
            outs[(size_t) c] = chunk.getWritePointer (c);

        int inPos = 0, outPos = 0;
        bool flushing = false;
        int guard = 0;
        const int maxIterations = 8 * (srcLen / 512 + 64);

        while (outPos < srcLen && ++guard < maxIterations)
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

                    const int give = juce::jmin (needed, srcLen - inPos);
                    for (int c = 0; c < numChannels; ++c)
                    {
                        if (give > 0)
                            feed.copyFrom (c, 0, in, c, srcStart + inPos, give);
                        ins[(size_t) c] = feed.getReadPointer (c);
                    }

                    produced = ts.processData (ins.data(), needed, outs.data());
                    inPos += needed;

                    if (inPos >= srcLen)
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

            const int take = juce::jmin (produced, srcLen - outPos);
            for (int c = 0; c < numChannels && take > 0; ++c)
                dest.copyFrom (c, outPos, chunk, c, 0, take);
            outPos += juce::jmax (0, take);
        }

        return outPos;
    }
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

    out.setSize (numChannels, numSamples);
    out.clear();

    // Abschnittsweise konstante Tonhoehe, ueberblendet statt umgeschaltet.
    // 120 ms sind fein genug, damit die Korrektur der Stimme folgt (menschliches
    // Vibrato liegt bei 5-7 Hz), und grob genug, dass pro Abschnitt nur EINE
    // Parameteraenderung noetig ist. Die Ueberblendung verdeckt den Schnitt.
    const int segLen   = juce::jmax (256, (int) (0.120 * sampleRate));
    const int fadeLen  = juce::jmax (64,  (int) (0.030 * sampleRate));
    // Vorlauf: SoundTouch braucht Anlauf, bis der Ausgang gefuellt ist. Dieser
    // Teil wird verworfen, damit der Abschnitt zeitrichtig sitzt.
    const int preRoll  = juce::jmax (128, (int) (0.060 * sampleRate));

    juce::AudioBuffer<float> piece;
    bool wroteAnything = false;

    for (int segStart = 0; segStart < numSamples; segStart += segLen)
    {
        const int wanted = juce::jmin (segLen + fadeLen, numSamples - segStart);
        if (wanted <= 0)
            break;

        // Mittlere Korrektur ueber den Abschnitt (die Kurve ist bereits
        // geglaettet, deshalb genuegt der Mittelwert).
        double sum = 0.0;
        int    n   = 0;
        for (int s = segStart; s < segStart + wanted; s += hopSize)
        {
            const int f = juce::jlimit (0, juce::jmax (0, correctionSemitones.size() - 1), s / hopSize);
            if (! correctionSemitones.isEmpty())
            {
                sum += correctionSemitones[f];
                ++n;
            }
        }
        const float semis = n > 0 ? (float) (sum / n) : 0.0f;

        const int ctx      = juce::jmin (preRoll, segStart);
        const int produced = renderConstantPitch (in, segStart - ctx, ctx + wanted,
                                                  semis, sampleRate, piece);
        if (produced <= ctx)
            continue;

        const int avail = juce::jmin (wanted, produced - ctx);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = piece.getReadPointer (c) + ctx;
            float*       dst = out.getWritePointer (c) + segStart;

            for (int i = 0; i < avail; ++i)
            {
                if (segStart > 0 && i < fadeLen)
                {
                    // Gleichleistungs-Ueberblendung mit dem vorigen Abschnitt:
                    // haelt die Lautstaerke ueber den Schnitt konstant.
                    const float t = (float) i / (float) fadeLen;
                    dst[i] = dst[i] * std::cos (t * juce::MathConstants<float>::halfPi)
                           + src[i] * std::sin (t * juce::MathConstants<float>::halfPi);
                }
                else
                {
                    dst[i] = src[i];
                }
            }
        }
        wroteAnything = true;
    }

    return wroteAnything;
}

//==============================================================================
// Clip-Workflow

te::WaveAudioClip* VocalTuner::tuneClipInPlace (te::WaveAudioClip& clip, const Settings& settings)
{
    auto* edit = editManager.getEdit();
    if (edit == nullptr || &clip.edit != edit)
        return nullptr;

    auto* track = dynamic_cast<te::AudioTrack*> (clip.getClipTrack());
    if (track == nullptr)
        return nullptr;

    // v1: nur ungestretchte Clips (Datei-Zeit == Edit-Zeit)
    if (std::abs (clip.getSpeedRatio() - 1.0) > 0.001 || clip.getAutoTempo())
        return nullptr;

    const auto range = clip.getPosition().time;
    if (range.getLength().inSeconds() <= 0.0)
        return nullptr;

    // Aktuelle Quelle lesen (bei aktivem Warp = die gewarpte Datei) —
    // genau den Ausschnitt, den der Clip spielt (offset..offset+len).
    const auto sourceFile = clip.getAudioFile().getFile();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return nullptr;

    const double rate     = reader->sampleRate;
    const auto   startSmp = (juce::int64) (clip.getPosition().getOffset().inSeconds() * rate);
    const auto   lenSmp   = juce::jmin ((juce::int64) (range.getLength().inSeconds() * rate),
                                        reader->lengthInSamples - startSmp);
    if (lenSmp < 4096)
        return nullptr;   // zu kurz für sinnvolle Analyse

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) lenSmp);
    reader->read (&buffer, 0, (int) lenSmp, startSmp, true, true);

    // Mono-Mix für die Analyse
    juce::AudioBuffer<float> mono (1, (int) lenSmp);
    mono.clear();
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        mono.addFrom (0, 0, buffer, c, 0, (int) lenSmp, 1.0f / (float) buffer.getNumChannels());

    PitchDetector::Settings ps;
    const auto pitch = PitchDetector::detectPitchHz (mono.getReadPointer (0), (int) lenSmp, rate, ps);
    if (pitch.isEmpty() || PitchDetector::medianPitchHz (pitch) <= 0.0f)
        return nullptr;   // kein tonales Material erkannt

    const auto corr = buildCorrectionSemitones (pitch, settings, rate / ps.hopSize);

    juce::AudioBuffer<float> tuned;
    if (! renderCorrected (buffer, tuned, rate, corr, ps.hopSize))
        return nullptr;

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
            return nullptr;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, (unsigned int) tuned.getNumChannels(), 24, {}, 0));
        if (writer == nullptr)
            return nullptr;
        stream.release();
        if (! writer->writeFromAudioSampleBuffer (tuned, 0, tuned.getNumSamples()))
            return nullptr;
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
        return nullptr;

    newClip->getSourceFileReference().setToDirectFileReference (outFile, false);

    juce::ValueTree parent = keepAlive->state.getParent();
    if (parent.isValid())
        parent.removeChild (keepAlive->state, &um);

    return newClip;
}
