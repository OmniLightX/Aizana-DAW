#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

// Forward declare so processors can call BuiltInPluginFormat::getDescriptionFor
// without a circular include — the full include is in the .cpp file.
class BuiltInPluginFormat;

enum class EQBandType { LowShelf, Peak, HighShelf, LowPass, HighPass, Notch };

struct EQBand
{
    EQBandType type      = EQBandType::Peak;
    float frequencyHz    = 1000.0f;
    float gainDB         = 0.0f;
    float Q              = 0.707f;
    bool  enabled        = true;
    int   slopeOrder     = 2;  // dB/oct slope for HP/LP: 2=12dB, 4=24dB/oct

    // --- Dynamik (Paket 2, 08/2026): das Band senkt zusaetzlich ab, sobald SEINE
    // Frequenz zu laut wird. Aus = klassischer statischer EQ, Bit fuer Bit wie vorher.
    bool  dynOn          = false;
    float dynThresholdDB = -18.0f;   // ab hier greift die Absenkung
    float dynRatio       = 2.0f;     // 1 = keine Wirkung, 10 = fast Deckel
};

/**
 * EQProcessor — 4-band parametric EQ using juce::dsp::ProcessorChain.
 *
 * Audio-thread safety:
 *   - Coefficients are computed on the calling thread in setBand() via a
 *     std::atomic exchange pattern (JUCE IIR filter coefficients are
 *     reference-counted SharedResourcePointers — assigning them is atomic
 *     in JUCE 7+/8).
 *   - processBlock() is allocation-free — no dynamic memory on audio thread.
 *
 * Spectrum data:
 *   - Audio thread writes samples into spectrumFifo (lock-free).
 *   - Message thread reads FIFO in a timer callback, runs FFT, repaints.
 *   - getSpectrumData() is a message-thread-only call.
 *
 * FX-01 requirement: minimum 4 independent parametric bands.
 */
class EQProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr int NumBands = 8;

    EQProcessor();
    ~EQProcessor() override = default;

    /** Set band parameters (may be called from any thread). */
    void setBand(int bandIndex, const EQBand& band);
    EQBand getBand(int bandIndex) const;

    /** Attack/Release der Dynamik — geraeteweit, nicht pro Band. Message-Thread. */
    void  setDynTimes (float attackMs, float releaseMs);
    float getDynAttackMs()  const;
    float getDynReleaseMs() const;

    /**
     * Copy spectrum magnitude data (in dB) into magnitudesOut.
     * Only call from message thread. Returns false if no new data.
     */
    bool getSpectrumData(std::vector<float>& magnitudesOut, int fftSize = 2048);

    // -------------------------------------------------------------------------
    // juce::AudioProcessor interface
    // -------------------------------------------------------------------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    void releaseResources() override {}

    // Required pure-virtual overrides (minimal implementation for DSP-only use)
    const juce::String getName() const override { return "EQProcessor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // AudioPluginInstance interface
    void fillInPluginDescription (juce::PluginDescription& desc) const override;

    /**
     * Berechnet die Biquad-Werte fuer ein Band — reine Arithmetik, RT-tauglich.
     *
     * Oeffentlich, weil zustandslos: der Test vergleicht sie gegen JUCEs
     * Referenzformeln (IIR::Coefficients::makeXXX). Ohne diesen Vergleich koennte
     * der Umbau auf den alloc-freien Weg den Klang aendern, ohne dass etwas
     * anschlaegt.
     */
    static std::array<float, 6> coefficientsForBand (const EQBand& b, double sampleRate);

    /** Schreibt die Werte in ein bestehendes Coefficients-Objekt (kein Alloc).
        Oeffentlich aus demselben Grund wie coefficientsForBand: der Test vergleicht
        das Ergebnis gegen JUCEs Referenz. */
    static void writeCoefficients (juce::dsp::IIR::Coefficients<float>& dest,
                                   const std::array<float, 6>& src);

private:
    // ProcessorDuplicator wraps the mono IIR::Filter for stereo (and beyond).
    // IIR::Filter<float> asserts numChannels==1 — using it directly in the chain
    // with a stereo AudioBlock silently skips channel 1 (no audio change on right).
    using EQFilter = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    using EQChain = juce::dsp::ProcessorChain<
        EQFilter,   // Band 0
        EQFilter,   // Band 1
        EQFilter,   // Band 2
        EQFilter,   // Band 3
        EQFilter,   // Band 4
        EQFilter,   // Band 5
        EQFilter,   // Band 6
        EQFilter>;  // Band 7

    EQChain eqChain;
    std::array<EQBand, NumBands> bands;
    double currentSampleRate = 44100.0;

    //==========================================================================
    // Automatisierbare Parameter (pro Band: An, Frequenz, Gain, Q)
    //
    // Bis 08/2026 hatte der EQ ueberhaupt keine AudioProcessorParameter: sein
    // Zustand lag nur im XML. Fuer Tracktion war er damit ein Plugin ohne
    // automatisierbare Parameter — im Automations-Dropdown erschienen nur die
    // Wrapper-Eintraege "Dry Level"/"Wet Level". Ein Filter-Sweep oder eine
    // Band-Gain-Automation war unmoeglich, waehrend alle anderen Built-ins
    // (APVTS) ihre Parameter vollstaendig anbieten.
    //
    // Typ und Flankensteilheit bleiben bewusst DRAUSSEN: es sind diskrete
    // Umschaltungen, die niemand automatisiert, und sie wuerden die Liste pro
    // Instanz um 16 Eintraege aufblaehen. Sie leben weiter im XML-State.
    //==========================================================================
    struct BandParams
    {
        juce::AudioParameterBool*  on   = nullptr;
        juce::AudioParameterFloat* freq = nullptr;
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterFloat* q    = nullptr;

        // Dynamik pro Band; Attack/Release gelten fuer alle Baender gemeinsam
        // (ein Regelverhalten fuer das Geraet, wie bei einem Kompressor).
        juce::AudioParameterBool*  dynOn     = nullptr;
        juce::AudioParameterFloat* dynThresh = nullptr;
        juce::AudioParameterFloat* dynRatio  = nullptr;
    };

    std::array<BandParams, NumBands> bandParams;

    juce::AudioParameterFloat* dynAttackMs  = nullptr;
    juce::AudioParameterFloat* dynReleaseMs = nullptr;

    /** Zuletzt in Koeffizienten umgesetzte Werte — Vergleich spart Rechenarbeit,
        wenn sich nichts bewegt (der Normalfall ohne Automation). */
    struct AppliedValues
    {
        float freq = -1.0f, gain = 0.0f, q = 0.0f;
        int   type = -1, slope = -1;
        bool  on   = false;
        // Muss mitverglichen werden: schaltet die Dynamik AUS, hat sich an den
        // statischen Werten nichts geaendert — ohne dieses Feld bliebe das Band
        // auf dem zuletzt dynamisch geschriebenen Gain stehen.
        bool  dyn  = false;
        bool  valid = false;
    };

    std::array<AppliedValues, NumBands> applied;

    /** Sample-Rate fuer den Audio-Thread (currentSampleRate wird vom
        Message-Thread in prepareToPlay geschrieben). */
    std::atomic<double> sampleRateForAudio { 44100.0 };

    //==========================================================================
    // Dynamik-Sektion
    //
    // Pro Band ein eigener Detektor: ein Bandpass auf Frequenz und Q des Bandes
    // ("was hoert dieses Band?"), dahinter eine Huellkurve mit Attack/Release.
    // Ueberschreitet sie die Schwelle, wird das Band um
    //     (Pegel - Schwelle) * (1 - 1/Ratio)
    // abgesenkt — dieselbe Rechnung wie im CompressorProcessor, nur dass das
    // Ergebnis nicht die Lautstaerke, sondern den Band-Gain steuert.
    //
    // Der Detektor hoert den EINGANG des EQs, nicht seinen Ausgang: sonst regelt
    // sich das Band an seiner eigenen Absenkung entlang und die Regelung schwingt.
    //
    // Koeffizienten werden in Teilbloecken von kDynSubBlock Samples nachgezogen —
    // pro Sample waere zu teuer, pro Block (bei 512 Samples ~12 ms) zu grob fuer
    // kurze Attacks.
    //==========================================================================
    static constexpr int kDynSubBlock = 32;

    struct DynState
    {
        juce::dsp::IIR::Filter<float> detector;                     // mono
        juce::dsp::IIR::Coefficients<float>::Ptr detectorCoeffs;    // einmal angelegt
        float envelope     = 0.0f;   // linear, 0..1+
        float appliedGain  = 0.0f;   // zuletzt in Koeffizienten geschriebener Gain (dB)
        float detFreq      = -1.0f;  // wofuer der Bandpass zuletzt gerechnet wurde
        float detQ         = -1.0f;
    };

    std::array<DynState, NumBands> dyn;

    /** Wirkt die Dynamik gerade? Getrennt vom WUNSCH des Users (bands[i].dynOn),
        der auch dann stehen bleibt, wenn das Band aus ist oder gar keinen Gain hat —
        sonst spraenge der Haken beim Anklicken sofort wieder heraus. */
    std::array<bool, NumBands> dynEffective {};

    float dynAttackCoeff  = 0.0f;   // pro Teilblock, aus Attack/Release + Samplerate
    float dynReleaseCoeff = 0.0f;
    float dynCoeffSourceAttack = -1.0f, dynCoeffSourceRelease = -1.0f;

    /** true, sobald irgendein Band Dynamik eingeschaltet hat (sonst laeuft der
        Block wie vorher in EINEM Stueck durch — keine Kosten, wenn ungenutzt). */
    bool anyDynamicActive = false;

    /** Huellkurven fortschreiben und, falls noetig, Koeffizienten neu schreiben.
        Laeuft je Teilblock im Audio-Thread; alloc- und lock-frei. */
    void updateDynamicsForSubBlock (const juce::AudioBuffer<float>& input,
                                    int startSample, int numSamples);

    /** Schreibt die Koeffizienten fuer ein Band mit gegebenem Gain (dB). */
    void writeBandCoefficients (int bandIndex, float gainDB, double sampleRate);

    /** Legt die Parameter an (nur aus dem Konstruktor). */
    void createBandParameters();

    /**
     * Liest die Parameter und schreibt geaenderte Koeffizienten in die Filter.
     *
     * LAEUFT IM AUDIO-THREAD und ist alloc-frei: die Werte kommen von
     * juce::dsp::ArrayCoefficients, das ein std::array<float,6> auf dem STACK
     * liefert (im Gegensatz zu IIR::Coefficients::makeXXX, das jedes Mal ein
     * referenzgezaehltes Objekt anlegt — genau das darf hier nicht passieren,
     * denn unter Automation liefe es in JEDEM Block).
     */
    void refreshCoefficientsFromParameters();




    // Secondary IIR filters for HP/LP slope 24dB/oct (slopeOrder=4).
    // Each cascadeFilter[i] mirrors the coefficients of eqChain band i.
    // cascadeActive[i] is true only when band i is HP/LP with slopeOrder >= 4.
    std::array<EQFilter, NumBands> cascadeFilters;
    std::array<bool, NumBands> cascadeActive {};

    // Lock-free FIFO for spectrum analyzer (RESEARCH.md Pitfall 6)
    // Audio thread pushes samples; message thread reads and runs FFT.
    static constexpr int SpectrumFifoSize = 4096;
    juce::AbstractFifo spectrumFifo { SpectrumFifoSize };
    juce::AudioBuffer<float> spectrumBuffer { 1, SpectrumFifoSize };

    /** Compute IIR coefficients and apply to the chain. Call from any thread. */
    void updateBandCoefficients(int bandIndex);

    /** Apply updated coefficients to the chain (called after setBand). */
    void applyCoefficients(int bandIndex,
        juce::dsp::IIR::Coefficients<float>::Ptr coeffs);
};
