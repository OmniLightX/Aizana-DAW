#include "TempoDetector.h"

#include <juce_dsp/juce_dsp.h>

#include <thread>
#include <vector>
#include <cmath>

namespace
{
    constexpr int    kHop          = 512;     // onset envelope frame hop (samples)
    constexpr double kMaxAnalysisS = 60.0;    // analyse at most the first minute
    constexpr double kMinBpm       = 60.0;
    constexpr double kMaxBpm       = 200.0;

    constexpr int kFftOrder = 11;              // 2048 Samples Analysefenster
    constexpr int kFftSize  = 1 << kFftOrder;

    // Verlässlichkeitsmaße (C2). Beide Werte sind an echtem Material vermessen
    // (Messreihe 2026-08-15, test_tempo_detector.cpp "[confidence]"), nicht
    // geraten — die Zahlen stehen dort im Testkommentar.
    constexpr double kOnsetSupportSd    = 0.5;   // SD-Einheiten: ab hier gilt ein
                                                 // Frame als echter Anschlag
    constexpr double kIntervalTolerance = 0.15;  // ±15 % um den Median-Abstand

    // Onset-Kurve = SPECTRAL FLUX (ein Wert pro Hop).
    //
    // Vorher stand hier die Differenz der breitbandigen Frame-ENERGIE. Das ist
    // billig, aber auf echter Musik unbrauchbar: die Energie wird vom Bass
    // dominiert, HiHats/Snares verschwinden darunter, und ohne Frequenzauflösung
    // sieht ein anschwellender Basston aus wie ein Anschlag. Auf echtem Material
    // (Hip-Hop, 121 BPM) fand der Detektor damit auf KEINER Quelle das richtige
    // Tempo — Messreihe 2026-08-10.
    //
    // Spectral Flux summiert stattdessen nur die POSITIVEN Änderungen des
    // log-komprimierten Magnitudenspektrums: jedes Band zählt gleich viel,
    // egal wie laut es absolut ist, und nur Anstiege (= Anschläge) zählen.
    // Das ist der Standard-Onset-Detektor, auf dem Ellis' Beat-Tracking aufsetzt.
    std::vector<double> computeOnsetFlux (const juce::AudioBuffer<float>& mono)
    {
        const int numSamples = mono.getNumSamples();
        const int numFrames  = numSamples / kHop;
        if (numFrames < 2)
            return {};

        juce::dsp::FFT fft (kFftOrder);
        juce::dsp::WindowingFunction<float> window ((size_t) kFftSize,
                                                    juce::dsp::WindowingFunction<float>::hann);

        const int numBins = kFftSize / 2;
        std::vector<float>  fftBuffer ((size_t) kFftSize * 2, 0.0f);
        std::vector<double> prevMag ((size_t) numBins, 0.0);
        std::vector<double> flux ((size_t) numFrames, 0.0);

        const float* in = mono.getReadPointer (0);

        for (int f = 0; f < numFrames; ++f)
        {
            // RÜCKSCHAU-Fenster: es ENDET am Ende dieses Hops, statt dort zu
            // beginnen. Zeigt das Fenster nach vorn, taucht ein Anschlag im
            // Spektrum auf, sobald er ins Fenster hineinrutscht — also bis zu
            // einer ganzen Fensterlänge (46 ms) BEVOR er wirklich klingt, und
            // alle Beats sitzen systematisch zu früh. (Real gefangen: der
            // 120-BPM-Klicktrack-Test fand 0 statt 29 Treffer auf den Klicks.)
            std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
            const int end   = juce::jmin (numSamples, f * kHop + kHop);
            const int start = end - kFftSize;
            const int from  = juce::jmax (0, start);
            const int count = end - from;
            if (count > 0)
                std::copy (in + from, in + end,
                           fftBuffer.begin() + (from - start));   // vorne mit Nullen auffüllen

            window.multiplyWithWindowingTable (fftBuffer.data(), (size_t) kFftSize);
            fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

            // Log-Kompression: ohne sie bestimmt der lauteste Schlag die ganze
            // Kurve und leisere Anschläge fallen unter die Wahrnehmungsschwelle
            // des Trackers.
            double sum = 0.0;
            for (int b = 0; b < numBins; ++b)
            {
                const double mag = std::log1p (100.0 * (double) fftBuffer[(size_t) b]);
                sum += juce::jmax (0.0, mag - prevMag[(size_t) b]);   // nur Anstiege
                prevMag[(size_t) b] = mag;
            }
            flux[(size_t) f] = sum;
        }

        // Der erste Frame ist per Konstruktion ein Anstieg aus dem Nichts —
        // sonst stünde am Dateianfang ein künstlicher Riesen-Onset.
        if (! flux.empty())
            flux[0] = 0.0;

        return flux;
    }

    // Global pulse estimate from the (raw) flux via autocorrelation; 0.0 = none
    double bpmFromFlux (std::vector<double> flux, double framesPerSecond)
    {
        const int numFrames = (int) flux.size();
        if (numFrames < 16)
            return 0.0;

        // Remove DC of the envelope so autocorrelation peaks on periodicity
        double mean = 0.0;
        for (double v : flux) mean += v;
        mean /= (double) flux.size();
        for (double& v : flux) v -= mean;

        const int minLag = juce::jmax (1, (int) std::floor (framesPerSecond * 60.0 / kMaxBpm));
        const int maxLag = (int) std::ceil (framesPerSecond * 60.0 / kMinBpm);
        if (maxLag >= numFrames / 2)
            return 0.0;

        // TEMPO-PRIOR statt 1/sqrt(lag)-Heuristik.
        //
        // Die alte Gewichtung bevorzugte pauschal kürzere Lags — sie brach zwar
        // Oktav-Gleichstände, zog den Detektor aber systematisch auf
        // Unterteilungen (auf echtem Material landete er auf 161 statt 121).
        // Ellis (2007) gewichtet stattdessen mit einer Gauss-Glocke auf der
        // LOG-Tempo-Achse um einen Erwartungswert: Halb-/Doppeltempo liegt je
        // eine Oktave daneben und wird dadurch klar bestraft, während normale
        // Tempi im breiten Zentrum praktisch ungewichtet bleiben.
        constexpr double kCentreBpm = 120.0;   // Erwartungswert
        constexpr double kSigmaOct  = 0.9;     // Breite in Oktaven (bewusst weit)

        std::vector<double> scores ((size_t) (maxLag + 1), 0.0);
        int bestLag = 0;
        double bestScore = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double score = 0.0;
            int terms = 0;
            for (int i = 0; i + lag < numFrames; ++i)
            {
                score += flux[(size_t) i] * flux[(size_t) (i + lag)];
                ++terms;
            }
            if (terms <= 0)
                continue;

            // Auf die Zahl der Summanden normieren: sonst gewinnen kurze Lags
            // allein deshalb, weil mehr Terme aufaddiert werden.
            score /= (double) terms;

            const double bpmHere = 60.0 * framesPerSecond / (double) lag;
            const double oct     = std::log2 (bpmHere / kCentreBpm);
            score *= std::exp (-0.5 * (oct / kSigmaOct) * (oct / kSigmaOct));

            scores[(size_t) lag] = score;
            if (score > bestScore)
            {
                bestScore = score;
                bestLag = lag;
            }
        }

        if (bestLag <= 0 || bestScore <= 0.0)
            return 0.0;

        // SUB-FRAME-GENAUIGKEIT durch parabolische Interpolation.
        // Lags sind ganzzahlige Frames: bei 86 Frames/s liegt der Lag für 120 BPM
        // bei 43.07 — waehlbar sind nur 43 (120.2 BPM) oder 42 (123.0 BPM). Ohne
        // Interpolation ist das Raster nahe 120 BPM also fast 3 BPM grob, und
        // exakte 121 BPM sind gar nicht darstellbar. Der Scheitel der Parabel
        // durch die drei Punkte um das Maximum liefert den Zwischenwert.
        double refinedLag = (double) bestLag;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const double yL = scores[(size_t) (bestLag - 1)];
            const double yC = scores[(size_t) bestLag];
            const double yR = scores[(size_t) (bestLag + 1)];
            const double denom = yL - 2.0 * yC + yR;
            if (std::abs (denom) > 1.0e-18)
            {
                const double delta = 0.5 * (yL - yR) / denom;
                if (std::abs (delta) <= 1.0)          // nur echte Scheitel uebernehmen
                    refinedLag = (double) bestLag + delta;
            }
        }

        const double bpm = 60.0 * framesPerSecond / refinedLag;

        // KEIN nachträgliches Oktav-Falten mehr: der Prior hat die Oktave
        // bereits gewählt: Falten würde diese Entscheidung wieder überschreiben.
        return juce::jlimit (kMinBpm, kMaxBpm, bpm);
    }

    // Mono mixdown of the first kMaxAnalysisS seconds; empty buffer on failure
    juce::AudioBuffer<float> loadMonoForAnalysis (const juce::File& audioFile)
    {
        if (! audioFile.existsAsFile())
            return {};

        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return {};

        const auto numSamples = (int) juce::jmin<juce::int64> (
            reader->lengthInSamples, (juce::int64) (reader->sampleRate * kMaxAnalysisS));
        if (numSamples < kHop * 8)
            return {};

        juce::AudioBuffer<float> stereo (juce::jmin (2, (int) reader->numChannels), numSamples);
        reader->read (&stereo, 0, numSamples, 0, true, stereo.getNumChannels() > 1);

        juce::AudioBuffer<float> mono (1, numSamples);
        mono.copyFrom (0, 0, stereo, 0, 0, numSamples);
        if (stereo.getNumChannels() > 1)
        {
            mono.addFrom (0, 0, stereo, 1, 0, numSamples);
            mono.applyGain (0.5f);
        }
        return mono;
    }

    double readerSampleRate (const juce::File& audioFile)
    {
        juce::AudioFormatManager afm;
        afm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (audioFile));
        return reader != nullptr ? reader->sampleRate : 0.0;
    }
}

//==============================================================================
double TempoDetector::detectBpm (const juce::AudioBuffer<float>& mono, double sampleRate)
{
    if (sampleRate <= 0.0 || mono.getNumSamples() < kHop * 8)
        return 0.0;

    return bpmFromFlux (computeOnsetFlux (mono), sampleRate / (double) kHop);
}

double TempoDetector::detectBpmInFile (const juce::File& audioFile)
{
    const auto mono = loadMonoForAnalysis (audioFile);
    if (mono.getNumSamples() == 0)
        return 0.0;

    return detectBpm (mono, readerSampleRate (audioFile));
}

void TempoDetector::detectAsync (const juce::File& audioFile,
                                 std::function<void (double)> onResult)
{
    std::thread ([audioFile, onResult = std::move (onResult)]
    {
        const double bpm = detectBpmInFile (audioFile);
        juce::MessageManager::callAsync ([bpm, onResult] { onResult (bpm); });
    }).detach();
}

//==============================================================================
// P7a: Beat-Tracking (Ellis 2007 dynamic programming)

TempoDetector::BeatResult
TempoDetector::detectBeatsWithConfidence (const juce::AudioBuffer<float>& mono,
                                          double sampleRate)
{
    if (sampleRate <= 0.0 || mono.getNumSamples() < kHop * 8)
        return {};

    const double framesPerSecond = sampleRate / (double) kHop;
    auto flux = computeOnsetFlux (mono);
    const int n = (int) flux.size();
    if (n < 16)
        return {};

    const double bpm = bpmFromFlux (flux, framesPerSecond);
    if (bpm <= 0.0)
        return {};

    const double tau = framesPerSecond * 60.0 / bpm;   // frames per beat (global)

    // Onset-Staerke in EINHEITEN DER STANDARDABWEICHUNG statt auf das Maximum
    // normiert.
    //
    // Die Normierung auf das Maximum macht die Skala vom lautesten Einzelschlag
    // des Stuecks abhaengig: ein einzelner Crash druckt alle anderen Onsets
    // nach unten, und ein absoluter Schwellwert bedeutet dann bei jedem Stueck
    // etwas anderes. Nach Abzug des Mittelwerts und Division durch die
    // Standardabweichung liegen echte Anschlaege materialunabhaengig bei etwa
    // 2-6, und die Grundkosten pro Beat unten sind eine sinnvolle Groesse.
    double mean = 0.0;
    for (double v : flux) mean += v;
    mean /= (double) flux.size();

    double var = 0.0;
    for (double v : flux) var += (v - mean) * (v - mean);
    var /= (double) flux.size();
    const double sd = std::sqrt (var);
    if (sd <= 0.0)
        return {};

    // Rektifiziert: nur ueberdurchschnittliche Frames sind Anschlag-Kandidaten,
    // alles darunter ist 0 und traegt nichts zur Kette bei.
    for (double& v : flux) v = juce::jmax (0.0, (v - mean) / sd);

    // DP (Ellis 2007, adaptiv): C(t) = O(t) + max(0, max_Δ [ C(t−Δ) − Strafe ]).
    // Die Strafe referenziert das INTERVALL DES VORGÄNGERS (Tempo darf driften/
    // springen — einmal teuer, danach billig) plus einen leichten Zug zum
    // globalen Puls τ (verhindert Subdivision-/Doppeltempo-Drift). Eine rein
    // globale Referenz zwang bei echten Tempowechseln (100→150) das Raster
    // auf Zwischenwerte — real im Test gefangen.
    // β war lange klein (0.3), weil ein starker globaler Zug damals Halb-Tempo
    // billiger machte als das Klick-für-Klick-Folgen. Diese Gefahr ist mit dem
    // engen Suchbereich unten (0.60…1.70·τ) strukturell weg — die halbe und die
    // doppelte Ebene sind gar nicht mehr wählbar. Deshalb darf β jetzt kräftig
    // ziehen und hält das Raster auf syncopiertem Material regelmässig.
    //
    // Vermessen an der Drum-Summe des Testmaterials (121 BPM) gegen den
    // Tempowechsel-Test (100→150), Anteil korrekter Viertel-Abstände:
    //   β=0.3 → 56/107   β=2 → 65/108   β=6 → 88/106   β=10 → 94/105
    //   β=20 → 101/103, ABER der Tempowechsel-Test bricht.
    // 6.0 ist der Kompromiss mit Sicherheitsabstand: regelmässig auf konstantem
    // Material, folgt echten Tempoänderungen weiterhin (Bruch erst bei ~20).
    constexpr double alphaLocal  = 4.0;
    constexpr double betaGlobal  = 6.0;

    // Grundkosten PRO BEAT. Ohne sie kostet es nichts, einen Beat auf Stille zu
    // setzen: der DP bestraft nur ungleichmaessige Abstaende, nicht Schlaege ins
    // Leere. Eine Doppeltempo-Kette sammelt dann denselben Onset-Ertrag wie die
    // richtige (sie trifft jeden zweiten Klick, die anderen bringen 0) und zahlt
    // pro Schritt weniger Strafe, wenn tau zufaellig naeher am halben Intervall
    // liegt — real gefangen im 100->150-Test: die erste Haelfte lief mit 198 BPM,
    // weil das globale tau von der 150er-Haelfte auf 34 Frames gezogen wurde und
    // damit NAEHER an 26 (Doppeltempo) lag als an den korrekten 52 Frames.
    // Der Vorsprung betrug dort 0.4 % — jede Kette mit mehr Beats zahlt jetzt
    // fuer jeden einzelnen, was diesen Gleichstand eindeutig aufloest.
    constexpr double kBeatCost = 0.5;
    // Suchbereich ENG um den globalen Puls. Vorher 0.40·tau … 2.20·tau — dieser
    // Bereich enthaelt Doppel- UND Halbtempo, der DP durfte also frei zwischen
    // metrischen Ebenen wechseln. Auf echtem Material tat er das auch: bei
    // korrektem tau (120.8 BPM) waren 157 von 181 Abstaenden ACHTEL, weil die
    // HiHats dort Onsets liefern und zwei Achtel mehr Ertrag bringen als ein
    // Viertel (Messung 2026-08-10, Drum-Summe).
    //
    // 0.60 … 1.70 schliesst die halbe (0.5) und die doppelte (2.0) Ebene
    // strukturell aus und laesst trotzdem reichlich Luft fuer echte
    // Tempoaenderungen: ein Sprung 150 -> 100 BPM entspricht 1.5·tau und passt.
    // Weiter reichende Spruenge innerhalb EINES Stuecks werden nicht verfolgt —
    // bewusste Grenze, dokumentiert im Header.
    const int dMin = juce::jmax (1, (int) std::floor (tau * 0.60));
    const int dMax = juce::jmax (dMin + 1, (int) std::ceil (tau * 1.70));

    std::vector<double> score ((size_t) n, 0.0);
    std::vector<int>    backlink ((size_t) n, -1);
    std::vector<int>    interval ((size_t) n, 0);   // Δ, das zu t führte (0 = Start)

    for (int t = 0; t < n; ++t)
    {
        double best = 0.0;
        int bestPrev = -1;
        int bestD = 0;

        for (int d = dMin; d <= juce::jmin (dMax, t); ++d)
        {
            const int prev = t - d;
            const double refLocal = interval[(size_t) prev] > 0
                                      ? (double) interval[(size_t) prev]
                                      : tau;

            const double logLocal  = std::log ((double) d / refLocal);
            const double logGlobal = std::log ((double) d / tau);
            const double cand = score[(size_t) prev]
                              - alphaLocal * logLocal  * logLocal
                              - betaGlobal * logGlobal * logGlobal;
            if (cand > best)
            {
                best = cand;
                bestPrev = prev;
                bestD = d;
            }
        }

        score[(size_t) t]    = flux[(size_t) t] - kBeatCost + best;
        backlink[(size_t) t] = bestPrev;
        interval[(size_t) t] = bestD;
    }

    // Endpunkt: bestes C im letzten 1.5·τ-Fenster, dann Backtrack
    int end = -1;
    double bestEnd = -1.0;
    for (int t = juce::jmax (0, n - (int) std::ceil (tau * 1.5)); t < n; ++t)
    {
        if (score[(size_t) t] > bestEnd)
        {
            bestEnd = score[(size_t) t];
            end = t;
        }
    }
    if (end < 0)
        return {};

    std::vector<int> beatFrames;
    for (int t = end; t >= 0; t = backlink[(size_t) t])
    {
        beatFrames.push_back (t);
        if (backlink[(size_t) t] < 0)
            break;
    }

    // Backtrack lief rückwärts; weniger als 2 Beats = kein verwertbarer Puls
    std::reverse (beatFrames.begin(), beatFrames.end());
    if (beatFrames.size() < 2)
        return {};

    BeatResult result;
    for (int t : beatFrames)
        result.beats.add ((double) t * (double) kHop / sampleRate);

    // ---- Verlässlichkeit (C2) ------------------------------------------------
    // 1) Onset-Support: liegt unter jedem Beat wirklich ein Anschlag?
    //    Der DP setzt Beats auch auf Stille — er bestraft nur ungleichmässige
    //    Abstände. Auf sparsamem Material (Kick allein) ist genau das der
    //    Fehlermodus: das Raster hält sich formal, hat aber nichts, woran es
    //    sich festmacht. flux ist hier bereits rektifiziert und in SD-Einheiten,
    //    echte Anschläge liegen materialunabhängig bei etwa 2–6.
    int supported = 0;
    for (int t : beatFrames)
    {
        double peak = 0.0;
        for (int k = juce::jmax (0, t - 1); k <= juce::jmin (n - 1, t + 1); ++k)
            peak = juce::jmax (peak, flux[(size_t) k]);

        if (peak >= kOnsetSupportSd)
            ++supported;
    }
    result.onsetSupport = (double) supported / (double) beatFrames.size();

    // 2) Abstands-Konsistenz: Anteil der Beat-Abstände nahe am Median.
    //    Fängt „Puls verloren, Raster eiert" auch dann, wenn zufällig Onsets
    //    unter den Beats liegen.
    std::vector<int> deltas;
    for (size_t i = 0; i + 1 < beatFrames.size(); ++i)
        deltas.push_back (beatFrames[i + 1] - beatFrames[i]);

    std::vector<int> sorted (deltas);
    std::sort (sorted.begin(), sorted.end());
    const double medianDelta = (double) sorted[sorted.size() / 2];

    int consistent = 0;
    for (int d : deltas)
        if (std::abs ((double) d - medianDelta) <= kIntervalTolerance * medianDelta)
            ++consistent;

    result.intervalConsistency = (double) consistent / (double) deltas.size();

    // Eine Kette ist so gut wie ihr schwächstes Glied — beide Fehlerbilder
    // treten unabhängig voneinander auf.
    result.confidence = juce::jmin (result.onsetSupport, result.intervalConsistency);

    return result;
}

juce::Array<double> TempoDetector::detectBeats (const juce::AudioBuffer<float>& mono,
                                                double sampleRate)
{
    return detectBeatsWithConfidence (mono, sampleRate).beats;
}

TempoDetector::BeatResult
TempoDetector::detectBeatsWithConfidenceInFile (const juce::File& audioFile)
{
    const auto mono = loadMonoForAnalysis (audioFile);
    if (mono.getNumSamples() == 0)
        return {};

    return detectBeatsWithConfidence (mono, readerSampleRate (audioFile));
}

void TempoDetector::detectBeatsWithConfidenceAsync (const juce::File& audioFile,
                                                    std::function<void (BeatResult)> onResult)
{
    std::thread ([audioFile, onResult = std::move (onResult)]
    {
        auto r = detectBeatsWithConfidenceInFile (audioFile);
        juce::MessageManager::callAsync ([r = std::move (r), onResult] { onResult (r); });
    }).detach();
}

juce::Array<double> TempoDetector::detectBeatsInFile (const juce::File& audioFile)
{
    const auto mono = loadMonoForAnalysis (audioFile);
    if (mono.getNumSamples() == 0)
        return {};

    return detectBeats (mono, readerSampleRate (audioFile));
}

void TempoDetector::detectBeatsAsync (const juce::File& audioFile,
                                      std::function<void (juce::Array<double>)> onResult)
{
    std::thread ([audioFile, onResult = std::move (onResult)]
    {
        auto beats = detectBeatsInFile (audioFile);
        juce::MessageManager::callAsync ([beats = std::move (beats), onResult]
                                         { onResult (beats); });
    }).detach();
}
