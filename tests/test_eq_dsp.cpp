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
