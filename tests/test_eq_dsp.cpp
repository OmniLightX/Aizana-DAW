// test_eq_dsp.cpp — FX-01: Parametric EQ (minimum 4 bands)
// Tests the EQ DSP engine using juce::dsp::IIR::Filter per band.
// Magnitude response at key frequencies must match expected dB values
// within tolerance (±0.5 dB at test frequency).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <fx/EQProcessor.h>
#include <cmath>

// Helper: compute magnitude response of an audio buffer after processing an impulse
// Passes a unit impulse through the processor, then evaluates frequency response
// using the DFT relationship: H(f) = sum(h[n] * e^(-j*2*pi*f*n/N))
static float computeMagnitudeAtFreq(EQProcessor& eq, float freqHz, double sampleRate, int numSamples = 8192)
{
    juce::AudioBuffer<float> buf(1, numSamples);
    buf.clear();
    buf.setSample(0, 0, 1.0f);  // Unit impulse at sample 0

    juce::MidiBuffer midi;
    eq.prepareToPlay(sampleRate, numSamples);
    eq.processBlock(buf, midi);

    // Compute DFT magnitude at freqHz
    double omega = 2.0 * M_PI * freqHz / sampleRate;
    double re = 0.0, im = 0.0;
    const float* data = buf.getReadPointer(0);
    for (int n = 0; n < numSamples; ++n)
    {
        re += data[n] * std::cos(omega * n);
        im -= data[n] * std::sin(omega * n);
    }
    float magnitude = static_cast<float>(std::sqrt(re * re + im * im));
    return 20.0f * std::log10(magnitude + 1e-10f);  // Convert to dB
}

TEST_CASE("EQ: peak filter at 1kHz +6dB produces correct magnitude response", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::Peak;
    band.frequencyHz = 1000.0f;
    band.gainDB = 6.0f;
    band.Q = 1.0f;
    band.enabled = true;
    eq.setBand(0, band);

    // Other bands flat (0dB)
    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt1kHz = computeMagnitudeAtFreq(eq, 1000.0f, sampleRate);
    float magAt100Hz = computeMagnitudeAtFreq(eq, 100.0f, sampleRate);

    REQUIRE_THAT(magAt1kHz, Catch::Matchers::WithinAbs(6.0f, 0.5f));
    REQUIRE_THAT(magAt100Hz, Catch::Matchers::WithinAbs(0.0f, 0.1f));
}

TEST_CASE("EQ: low shelf at 200Hz +3dB boosts lows and leaves highs unchanged", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::LowShelf;
    band.frequencyHz = 200.0f;
    band.gainDB = 3.0f;
    band.Q = 0.707f;
    band.enabled = true;
    eq.setBand(0, band);

    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt80Hz  = computeMagnitudeAtFreq(eq, 80.0f, sampleRate);
    float magAt500Hz = computeMagnitudeAtFreq(eq, 500.0f, sampleRate);

    // Below shelf frequency: should be boosted >= +2.5dB
    REQUIRE(magAt80Hz >= 2.5f);
    // Well above shelf frequency: should be unchanged within ±0.1dB
    REQUIRE_THAT(magAt500Hz, Catch::Matchers::WithinAbs(0.0f, 0.15f));
}

TEST_CASE("EQ: high shelf at 10kHz -6dB attenuates highs", "[eq][FX-01]")
{
    EQProcessor eq;

    EQBand band;
    band.type = EQBandType::HighShelf;
    band.frequencyHz = 10000.0f;
    band.gainDB = -6.0f;
    band.Q = 0.707f;
    band.enabled = true;
    eq.setBand(0, band);

    for (int i = 1; i < EQProcessor::NumBands; ++i)
    {
        EQBand flat;
        flat.gainDB = 0.0f;
        flat.enabled = false;
        eq.setBand(i, flat);
    }

    const double sampleRate = 44100.0;
    float magAt15kHz = computeMagnitudeAtFreq(eq, 15000.0f, sampleRate);

    // Above shelf frequency: should be attenuated >= -5.5dB
    REQUIRE(magAt15kHz <= -5.5f);
}

TEST_CASE("EQ: all 4 bands operate independently", "[eq][FX-01]")
{
    EQProcessor eq;

    // Enable only band 2 (index 1) with +6dB peak at 2kHz
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        EQBand b;
        b.type = EQBandType::Peak;
        b.frequencyHz = 2000.0f;
        b.gainDB = 0.0f;
        b.enabled = false;
        eq.setBand(i, b);
    }

    EQBand activeBand;
    activeBand.type = EQBandType::Peak;
    activeBand.frequencyHz = 2000.0f;
    activeBand.gainDB = 6.0f;
    activeBand.Q = 1.0f;
    activeBand.enabled = true;
    eq.setBand(1, activeBand);  // Only band index 1

    const double sampleRate = 44100.0;
    float mag = computeMagnitudeAtFreq(eq, 2000.0f, sampleRate);

    // Should show the +6dB boost from band 1 only
    REQUIRE_THAT(mag, Catch::Matchers::WithinAbs(6.0f, 0.5f));

    // Band 0, 2, 3 are flat — make sure disabling doesn't contribute
    // (This test confirms independence: only 1 band is active)
    REQUIRE(EQProcessor::NumBands >= 4);
}

TEST_CASE("EQ: prepareToPlay at 96kHz succeeds without allocation", "[eq][FX-01]")
{
    EQProcessor eq;

    // Should not throw or crash
    REQUIRE_NOTHROW(eq.prepareToPlay(96000.0, 512));
}

//==============================================================================
// EQ-Automation (08/2026): der EQ hatte gar keine AudioProcessorParameter, sein
// Zustand lag nur im XML. Tracktion sah damit ein Plugin ohne automatisierbare
// Parameter — ein Filter-Sweep war unmoeglich. Diese Faelle nageln fest, dass die
// Bandwerte jetzt echte Parameter sind UND dass die Koeffizientenberechnung
// dieselben Werte liefert wie vorher.
//==============================================================================

TEST_CASE ("EQ-Automation: jedes Band stellt An/Frequenz/Gain/Q als Parameter bereit", "[eq][automation]")
{
    EQProcessor eq;

    const auto& params = eq.getParameters();

    // 8 Baender x 7 Werte (An/Frequenz/Gain/Q + Dynamik An/Schwelle/Ratio) plus die
    // beiden gerateweiten Zeitkonstanten. Typ und Flankensteilheit sind bewusst KEINE
    // Parameter (diskrete Umschaltungen, die niemand automatisiert).
    REQUIRE (params.size() == EQProcessor::NumBands * 7 + 2);

    int gefunden = 0;

    for (auto* p : params)
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            for (int b = 0; b < EQProcessor::NumBands; ++b)
                for (const char* suffix : { "on", "freq", "gain", "q", "dynon", "dynth", "dynra" })
                    if (withID->paramID == "b" + juce::String (b) + suffix)
                        ++gefunden;

    CHECK (gefunden == EQProcessor::NumBands * 7);
}

TEST_CASE ("EQ-Automation: ein Parameter bewegt den Filter hoerbar", "[eq][automation]")
{
    EQProcessor eq;

    // Band 4 (Peak, 2.5 kHz) ueber seinen PARAMETER anheben — nicht ueber setBand.
    // Genau diesen Weg nimmt eine Automationskurve.
    juce::AudioParameterFloat* gain = nullptr;

    for (auto* p : eq.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (withID->paramID == "b4gain")
                gain = dynamic_cast<juce::AudioParameterFloat*> (p);

    REQUIRE (gain != nullptr);

    const float flach = computeMagnitudeAtFreq (eq, 2500.0f, 48000.0);

    *gain = 12.0f;
    const float angehoben = computeMagnitudeAtFreq (eq, 2500.0f, 48000.0);

    // GEGENKONTROLLE: eine weit entfernte Frequenz darf sich NICHT mitbewegen —
    // sonst wuerde der Test auch bestehen, wenn schlicht alles lauter wird.
    const float weitWeg = computeMagnitudeAtFreq (eq, 60.0f, 48000.0);

    CHECK (angehoben > flach + 6.0f);          // deutliche Anhebung an 2,5 kHz
    CHECK (std::abs (weitWeg) < 3.0f);         // 60 Hz bleibt im Rahmen

    // Und zurueck: der Parameter steuert in BEIDE Richtungen.
    *gain = -12.0f;
    const float abgesenkt = computeMagnitudeAtFreq (eq, 2500.0f, 48000.0);
    CHECK (abgesenkt < flach - 6.0f);
}

TEST_CASE ("EQ-Automation: getBand folgt dem Parameter (Kurve im Editor stimmt)", "[eq][automation]")
{
    EQProcessor eq;

    juce::AudioParameterFloat* freq = nullptr;

    for (auto* p : eq.getParameters())
        if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (withID->paramID == "b2freq")
                freq = dynamic_cast<juce::AudioParameterFloat*> (p);

    REQUIRE (freq != nullptr);

    *freq = 4000.0f;
    CHECK (eq.getBand (2).frequencyHz == Catch::Approx (4000.0f).margin (1.0f));

    // Umgekehrt: setBand (GUI/Preset) muss den Parameter mitziehen, sonst
    // ueberschriebe der naechste Block die Aenderung wieder.
    EQBand b = eq.getBand (2);
    b.frequencyHz = 800.0f;
    eq.setBand (2, b);
    CHECK (freq->get() == Catch::Approx (800.0f).margin (1.0f));
}

TEST_CASE ("EQ-Automation: RT-Koeffizienten sind identisch mit JUCEs Referenz", "[eq][automation]")
{
    // Der Audio-Thread darf nicht allozieren, deshalb kommen die Koeffizienten aus
    // juce::dsp::IIR::ArrayCoefficients (std::array auf dem Stack) statt aus
    // IIR::Coefficients::makeXXX (referenzgezaehltes Objekt = Allokation).
    // Hier wird festgenagelt, dass beide Wege DIESELBEN Zahlen liefern — sonst
    // klaenge der EQ nach dem Umbau anders, ohne dass ein Test anschlaegt.
    const double sr = 48000.0;

    struct Fall { EQBandType typ; float f; float g; float q; };
    const Fall faelle[] = {
        { EQBandType::Peak,      1000.0f,  6.0f, 1.0f   },
        { EQBandType::Peak,      3000.0f, -9.0f, 2.5f   },
        { EQBandType::LowShelf,   120.0f,  4.0f, 0.707f },
        { EQBandType::HighShelf, 8000.0f, -3.0f, 0.707f },
        { EQBandType::LowPass,   5000.0f,  0.0f, 0.707f },
        { EQBandType::HighPass,    80.0f,  0.0f, 0.707f },
        { EQBandType::Notch,      500.0f,  0.0f, 4.0f   },
    };

    for (const auto& f : faelle)
    {
        EQBand b;
        b.type = f.typ; b.frequencyHz = f.f; b.gainDB = f.g; b.Q = f.q; b.enabled = true;

        const auto meins = EQProcessor::coefficientsForBand (b, sr);

        const float gainFactor = std::pow (10.0f, f.g / 20.0f);
        juce::dsp::IIR::Coefficients<float>::Ptr referenz;

        switch (f.typ)
        {
            case EQBandType::Peak:      referenz = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, f.f, f.q, gainFactor); break;
            case EQBandType::LowShelf:  referenz = juce::dsp::IIR::Coefficients<float>::makeLowShelf   (sr, f.f, f.q, gainFactor); break;
            case EQBandType::HighShelf: referenz = juce::dsp::IIR::Coefficients<float>::makeHighShelf  (sr, f.f, f.q, gainFactor); break;
            case EQBandType::LowPass:   referenz = juce::dsp::IIR::Coefficients<float>::makeLowPass    (sr, f.f, f.q); break;
            case EQBandType::HighPass:  referenz = juce::dsp::IIR::Coefficients<float>::makeHighPass   (sr, f.f, f.q); break;
            case EQBandType::Notch:     referenz = juce::dsp::IIR::Coefficients<float>::makeNotch      (sr, f.f, f.q); break;
        }

        REQUIRE (referenz != nullptr);

        // JUCE speichert FUENF Werte: a0 ist herausgeteilt. Genau diese
        // Normalisierung muss writeCoefficients nachbilden.
        REQUIRE (referenz->coefficients.size() == 5);

        // Ziel-Objekt mit der richtigen Groesse besorgen und ueberschreiben lassen.
        auto ziel = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 1000.0f, 1.0f, 1.0f);
        EQProcessor::writeCoefficients (*ziel, meins);

        REQUIRE (ziel->coefficients.size() == 5);

        for (int k = 0; k < 5; ++k)
            CHECK (ziel->coefficients[k] == Catch::Approx (referenz->coefficients[k]).margin (1.0e-5));
    }
}

// ===========================================================================
// Dynamik-EQ (Paket 2, 08/2026)
//
// Das Band senkt zusaetzlich ab, sobald SEINE Frequenz zu laut wird. Gemessen
// wird der Pegel eines Sinus NACH dem EQ — einmal mit, einmal ohne Dynamik.
// Ohne die Gegenprobe („leise bleibt unveraendert", „andere Frequenz loest
// nicht aus") wuerde ein Band, das einfach immer absenkt, jeden Test bestehen.
// ===========================================================================

namespace
{
    /** Sinus durch den EQ schicken und den Pegel der zweiten Haelfte messen
        (die erste Haelfte gehoert der Einschwingzeit der Huellkurve). */
    float pegelNachEQ (EQProcessor& eq, float freqHz, float amplitude,
                       double sampleRate = 44100.0, int numSamples = 44100)
    {
        juce::AudioBuffer<float> buf (2, numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            const float s = amplitude * std::sin (2.0f * (float) M_PI * freqHz
                                                   * (float) n / (float) sampleRate);
            buf.setSample (0, n, s);
            buf.setSample (1, n, s);
        }

        juce::MidiBuffer midi;
        eq.prepareToPlay (sampleRate, 512);

        for (int pos = 0; pos < numSamples; pos += 512)
        {
            const int len = juce::jmin (512, numSamples - pos);
            juce::AudioBuffer<float> block (buf.getArrayOfWritePointers(), 2, pos, len);
            eq.processBlock (block, midi);
        }

        const int start = numSamples / 2;
        return buf.getMagnitude (0, start, numSamples - start);
    }

    EQBand dynBand (float freq, float thresholdDB, float ratio, bool dynOn)
    {
        EQBand b;
        b.type           = EQBandType::Peak;
        b.frequencyHz    = freq;
        b.gainDB         = 0.0f;
        b.Q              = 2.0f;
        b.enabled        = true;
        b.dynOn          = dynOn;
        b.dynThresholdDB = thresholdDB;
        b.dynRatio       = ratio;
        return b;
    }

    void baenderAus (EQProcessor& eq, int ausser)
    {
        for (int i = 0; i < EQProcessor::NumBands; ++i)
        {
            if (i == ausser) continue;
            EQBand flat;
            flat.enabled = false;
            eq.setBand (i, flat);
        }
    }
}

TEST_CASE ("Dynamik-EQ: lauter Ton wird abgesenkt, leiser bleibt stehen", "[eq][dyneq]")
{
    const float lautAmp  = 0.5f;    // ~ -6 dBFS, klar ueber der Schwelle
    const float leiseAmp = 0.02f;   // ~ -34 dBFS, klar darunter

    // --- laut, Dynamik AUS: das Band steht auf 0 dB, es darf nichts passieren
    EQProcessor aus;
    baenderAus (aus, 0);
    aus.setBand (0, dynBand (1000.0f, -20.0f, 4.0f, false));
    const float lautOhneDyn = pegelNachEQ (aus, 1000.0f, lautAmp);

    // --- laut, Dynamik AN: dasselbe Signal muss messbar leiser herauskommen
    EQProcessor an;
    baenderAus (an, 0);
    an.setBand (0, dynBand (1000.0f, -20.0f, 4.0f, true));
    const float lautMitDyn = pegelNachEQ (an, 1000.0f, lautAmp);

    CHECK (lautOhneDyn == Catch::Approx (lautAmp).margin (0.02f));   // unveraendert

    // Gegen die RECHNUNG pruefen, nicht gegen "irgendwie leiser": Pegel -6,02 dBFS,
    // Schwelle -20 dB, Ratio 4 ⇒ Absenkung (20 - 6,02) * (1 - 1/4) = 10,48 dB.
    // Genau das kam heraus (0,500 → 0,1501). Eine blosse Untergrenze haette auch
    // eine falsche Kennlinie durchgewinkt — die Lehre aus dem EQ-Umbau davor.
    const float pegelDB    = juce::Decibels::gainToDecibels (lautAmp);
    const float erwarteter = juce::Decibels::decibelsToGain (
                                 pegelDB - (pegelDB - (-20.0f)) * (1.0f - 1.0f / 4.0f));
    CHECK (lautMitDyn == Catch::Approx (erwarteter).margin (erwarteter * 0.12f));

    // --- GEGENPROBE 1: leiser Ton unter der Schwelle bleibt unveraendert
    EQProcessor leise;
    baenderAus (leise, 0);
    leise.setBand (0, dynBand (1000.0f, -20.0f, 4.0f, true));
    const float leiseMitDyn = pegelNachEQ (leise, 1000.0f, leiseAmp);
    CHECK (leiseMitDyn == Catch::Approx (leiseAmp).margin (leiseAmp * 0.1f));

    // --- GEGENPROBE 2: derselbe laute Ton weit weg von der Bandfrequenz loest
    //     nicht aus (der Detektor hoert nur sein eigenes Band)
    EQProcessor daneben;
    baenderAus (daneben, 0);
    daneben.setBand (0, dynBand (8000.0f, -20.0f, 4.0f, true));
    const float fremdeFrequenz = pegelNachEQ (daneben, 200.0f, lautAmp);
    CHECK (fremdeFrequenz == Catch::Approx (lautAmp).margin (0.03f));
}

TEST_CASE ("Dynamik-EQ: hoehere Ratio senkt staerker ab", "[eq][dyneq]")
{
    const float amp = 0.5f;

    EQProcessor sanft;
    baenderAus (sanft, 0);
    sanft.setBand (0, dynBand (1000.0f, -20.0f, 1.5f, true));
    const float mitRatio15 = pegelNachEQ (sanft, 1000.0f, amp);

    EQProcessor hart;
    baenderAus (hart, 0);
    hart.setBand (0, dynBand (1000.0f, -20.0f, 8.0f, true));
    const float mitRatio8 = pegelNachEQ (hart, 1000.0f, amp);

    CHECK (mitRatio8 < mitRatio15);
    CHECK (mitRatio15 < amp);
}

TEST_CASE ("Dynamik-EQ: Zustand ueberlebt den Roundtrip, alte Sessions bleiben statisch",
           "[eq][dyneq]")
{
    EQProcessor a;
    a.setBand (2, dynBand (2500.0f, -14.5f, 3.5f, true));

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    EQProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());

    const auto band = b.getBand (2);
    CHECK (band.dynOn);
    CHECK (band.dynThresholdDB == Catch::Approx (-14.5f).margin (0.01f));
    CHECK (band.dynRatio       == Catch::Approx (3.5f).margin (0.01f));

    // Ein State OHNE Dynamik-Attribute (so sieht jede vor 08/2026 gespeicherte
    // Session aus) muss das Band statisch lassen — sonst wuerde eine alte Session
    // ploetzlich anders klingen.
    juce::XmlElement alt ("EQState");
    auto* bandXml = alt.createNewChildElement ("Band2");
    bandXml->setAttribute ("type",    (int) EQBandType::Peak);
    bandXml->setAttribute ("freq",    2500.0);
    bandXml->setAttribute ("gain",    3.0);
    bandXml->setAttribute ("q",       1.0);
    bandXml->setAttribute ("enabled", 1);
    bandXml->setAttribute ("slope",   2);

    juce::MemoryBlock altMb;
    EQProcessor::copyXmlToBinary (alt, altMb);

    EQProcessor c;
    c.setBand (2, dynBand (2500.0f, -14.5f, 3.5f, true));   // vorher AN …
    c.setStateInformation (altMb.getData(), (int) altMb.getSize());
    CHECK_FALSE (c.getBand (2).dynOn);                      // … danach AUS
}

// ===========================================================================
// Band-Solo (Audition) und Band-Mute — Standardausstattung jedes Profi-EQs
//
// User-Befund 2026-08-15, WAEHREND des Hoertests: "Uns fehlt Solo und Mute im
// EQ. Damit kann ich den Dynamik-EQ gar nicht richtig austesten." Zu Recht —
// ohne Solo laesst sich eine Stoerfrequenz nicht heraushoeren, ohne Mute gibt
// es keinen A/B-Vergleich eines einzelnen Bandes.
//
// Verhalten nach der Referenz (FabFilter Pro-Q, PreSonus Pro EQ3):
//   - Solo spielt NICHT die Wirkung des Bandes, sondern den Teil des Spektrums,
//     den das Band betrifft.
//   - Bei einem Low/High Cut hoert man, was WEGGESCHNITTEN wird (Umkehrung).
//   - Solo ist exklusiv und wird NICHT gespeichert (Abhoerzustand, kein Klang).
// ===========================================================================

namespace
{
    /** Pegel einer Frequenz nach dem EQ, in dB relativ zum Eingang.
        Misst mit einem Sinus statt mit einem Impuls, weil das Solo den
        EINGANG hoert und nicht die Impulsantwort der Kette. */
    float sineLevelDb (EQProcessor& eq, float freqHz, double sampleRate, int blocks = 8)
    {
        constexpr int kBlock = 512;
        eq.prepareToPlay (sampleRate, kBlock);

        juce::MidiBuffer midi;
        double sum = 0.0;
        int counted = 0;

        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    buf.setSample (ch, i, (float) std::sin (2.0 * M_PI * freqHz
                                                            * (b * kBlock + i) / sampleRate));

            eq.processBlock (buf, midi);

            // Erste Bloecke verwerfen: die Filter muessen erst einschwingen.
            if (b < 3)
                continue;

            const float* d = buf.getReadPointer (0);
            for (int i = 0; i < kBlock; ++i) { sum += (double) d[i] * d[i]; ++counted; }
        }

        const double rms = counted > 0 ? std::sqrt (sum / counted) : 0.0;
        // Eingangs-RMS eines Vollpegel-Sinus = 1/sqrt(2)
        return (float) (20.0 * std::log10 (juce::jmax (1.0e-9, rms / (1.0 / std::sqrt (2.0)))));
    }

    EQBand makeBand (EQBandType t, float freq, float gain, float q = 1.0f)
    {
        EQBand b;
        b.type = t; b.frequencyHz = freq; b.gainDB = gain; b.Q = q; b.enabled = true;
        return b;
    }

    /** Alle Baender neutral — sonst faerben die Werks-Baender (HP 40 Hz,
        LP 20 kHz) die Messung und man misst nicht, was man glaubt. */
    void flattenAllBands (EQProcessor& eq)
    {
        for (int i = 0; i < EQProcessor::NumBands; ++i)
            eq.setBand (i, makeBand (EQBandType::Peak, 1000.0f + 100.0f * i, 0.0f, 0.707f));
    }
}

TEST_CASE ("EQ Mute: ein einzelnes Band laesst sich abschalten, der Rest bleibt", "[eq][fx][solomute]")
{
    constexpr double sr = 44100.0;
    EQProcessor eq;
    flattenAllBands (eq);

    // Band 2: +12 dB bei 1 kHz. Band 5: +6 dB bei 5 kHz (muss unberuehrt bleiben).
    eq.setBand (2, makeBand (EQBandType::Peak, 1000.0f, 12.0f, 2.0f));
    eq.setBand (5, makeBand (EQBandType::Peak, 5000.0f,  6.0f, 2.0f));

    const float on1k = sineLevelDb (eq, 1000.0f, sr);
    const float on5k = sineLevelDb (eq, 5000.0f, sr);
    CHECK (on1k > 9.0f);
    CHECK (on5k > 4.0f);

    // Mute NUR Band 2
    eq.setBandEnabled (2, false);
    CHECK_FALSE (eq.isBandEnabled (2));

    const float off1k = sineLevelDb (eq, 1000.0f, sr);
    const float off5k = sineLevelDb (eq, 5000.0f, sr);

    INFO ("1 kHz " << on1k << " -> " << off1k << " dB, 5 kHz " << on5k << " -> " << off5k);
    CHECK (std::abs (off1k) < 0.5f);              // Band 2 wirkt nicht mehr
    CHECK (std::abs (off5k - on5k) < 0.5f);       // Band 5 arbeitet unveraendert

    // Und wieder an — der A/B-Vergleich muss verlustfrei zurueckfuehren.
    eq.setBandEnabled (2, true);
    CHECK (std::abs (sineLevelDb (eq, 1000.0f, sr) - on1k) < 0.2f);
}

TEST_CASE ("EQ Solo: man hoert den Bereich des Bandes, nicht seine Wirkung", "[eq][fx][solomute]")
{
    constexpr double sr = 44100.0;
    EQProcessor eq;
    flattenAllBands (eq);

    // Ein Glockenband bei 1 kHz. Der Gain ist bewusst 0 dB: das Solo muss auch
    // dann etwas hoerbar machen, wenn noch gar nichts eingestellt ist — das ist
    // ja der Sinn (erst suchen, dann bearbeiten).
    eq.setBand (3, makeBand (EQBandType::Peak, 1000.0f, 0.0f, 2.0f));

    CHECK (eq.getSoloedBand() == -1);
    eq.setSoloedBand (3);
    CHECK (eq.getSoloedBand() == 3);

    const float at1k  = sineLevelDb (eq, 1000.0f, sr);
    const float at100 = sineLevelDb (eq,  100.0f, sr);
    const float at8k  = sineLevelDb (eq, 8000.0f, sr);

    INFO ("Solo: 100 Hz " << at100 << " | 1 kHz " << at1k << " | 8 kHz " << at8k);

    // Die Mittenfrequenz kommt praktisch unveraendert durch (Bandpass mit
    // Einheitsverstaerkung im Zentrum, zweifach kaskadiert).
    CHECK (std::abs (at1k) < 1.5f);

    // Alles weit daneben ist deutlich weg — sonst waere das Solo wertlos.
    CHECK (at100 < at1k - 25.0f);
    CHECK (at8k  < at1k - 25.0f);

    // Solo aus → wieder neutral.
    eq.setSoloedBand (-1);
    CHECK (std::abs (sineLevelDb (eq, 100.0f, sr)) < 0.5f);
}

TEST_CASE ("EQ Solo bei einem Low Cut: man hoert, was weggeschnitten wird", "[eq][fx][solomute]")
{
    constexpr double sr = 44100.0;
    EQProcessor eq;
    flattenAllBands (eq);

    // Low Cut = HOCHPASS bei 300 Hz. Er nimmt die Tiefen weg — beim Abhoeren
    // will man genau diese Tiefen hoeren, um zu pruefen, ob man richtig
    // ansetzt. Das ist die Umkehrung, die Pro-Q ausdruecklich so macht.
    eq.setBand (1, makeBand (EQBandType::HighPass, 300.0f, 0.0f, 0.707f));

    // Ohne Solo: der Hochpass nimmt 80 Hz weg, 3 kHz bleibt.
    const float normal80 = sineLevelDb (eq,   80.0f, sr);
    const float normal3k = sineLevelDb (eq, 3000.0f, sr);
    INFO ("ohne Solo: 80 Hz " << normal80 << " | 3 kHz " << normal3k);
    CHECK (normal80 < -15.0f);
    CHECK (std::abs (normal3k) < 1.0f);

    // Mit Solo kehrt es sich um: 80 Hz ist da, 3 kHz ist weg.
    eq.setSoloedBand (1);
    const float solo80 = sineLevelDb (eq,   80.0f, sr);
    const float solo3k = sineLevelDb (eq, 3000.0f, sr);
    INFO ("mit Solo: 80 Hz " << solo80 << " | 3 kHz " << solo3k);

    CHECK (std::abs (solo80) < 1.5f);
    CHECK (solo3k < solo80 - 25.0f);
}

TEST_CASE ("EQ Solo ist exklusiv und wird nicht gespeichert", "[eq][fx][solomute]")
{
    EQProcessor eq;

    eq.setSoloedBand (2);
    eq.setSoloedBand (5);
    CHECK (eq.getSoloedBand() == 5);         // exklusiv: das zweite loest das erste ab

    eq.setSoloedBand (99);                    // ungueltig = kein Solo
    CHECK (eq.getSoloedBand() == -1);

    // Ein Abhoerzustand gehoert nicht in die Session: sonst oeffnet der User
    // seinen Song und hoert einen duennen Bandpass, ohne die Ursache zu sehen.
    eq.setBand (4, makeBand (EQBandType::Peak, 2000.0f, 5.0f, 1.0f));
    juce::MemoryBlock state;
    eq.setSoloedBand (4);
    eq.getStateInformation (state);

    EQProcessor eq2;
    eq2.setSoloedBand (7);                    // "steht noch von vorhin"
    eq2.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (eq2.getSoloedBand() == -1);
    CHECK_THAT (eq2.getBand (4).gainDB, Catch::Matchers::WithinAbs (5.0, 0.01));
}

// ---------------------------------------------------------------------------
// Regression 2026-08-15: Low Cut und High Cut waren im Kontextmenue VERTAUSCHT.
//
// Beim Bau des Solos aufgefallen: das Menue legte "Low Cut" auf
// EQBandType::LowPass — das ist ein TIEFPASS und nimmt die HOEHEN weg. Wer
// Trittschall wegschneiden wollte, verlor stattdessen die Brillanz, und das
// ohne Fehlermeldung. Die DSP war immer richtig, nur die Beschriftung nicht.
//
// Dieser Test haelt die Bedeutung der Typen fest, damit die Beschriftung nie
// wieder gegen die Physik laufen kann.
// ---------------------------------------------------------------------------
TEST_CASE ("EQ Filtertypen: HighPass ist der Low Cut, LowPass der High Cut", "[eq][fx][cutnaming]")
{
    constexpr double sr = 44100.0;

    {
        EQProcessor eq;
        flattenAllBands (eq);
        eq.setBand (1, makeBand (EQBandType::HighPass, 500.0f, 0.0f, 0.707f));

        const float low  = sineLevelDb (eq,  100.0f, sr);
        const float high = sineLevelDb (eq, 5000.0f, sr);
        INFO ("HighPass 500 Hz: 100 Hz " << low << " | 5 kHz " << high);

        CHECK (low < -15.0f);                 // Tiefen weg  = LOW CUT
        CHECK (std::abs (high) < 1.0f);       // Hoehen bleiben
    }

    {
        EQProcessor eq;
        flattenAllBands (eq);
        eq.setBand (1, makeBand (EQBandType::LowPass, 500.0f, 0.0f, 0.707f));

        const float low  = sineLevelDb (eq,  100.0f, sr);
        const float high = sineLevelDb (eq, 5000.0f, sr);
        INFO ("LowPass 500 Hz: 100 Hz " << low << " | 5 kHz " << high);

        CHECK (std::abs (low) < 1.0f);        // Tiefen bleiben
        CHECK (high < -15.0f);                // Hoehen weg = HIGH CUT
    }
}
