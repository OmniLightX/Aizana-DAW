#include "EQProcessor.h"
#include "plugins/BuiltInPluginFormat.h"
#include "ui/eq/EQComponent.h"

// ---------------------------------------------------------------------------
// EQEditor — AudioProcessorEditor wrapper for EQComponent + preset bar
// ---------------------------------------------------------------------------

// Built-in preset table — defined here so EQEditor can reference it without
// exposing it in the public header.
struct EQPreset
{
    juce::String name;
    std::array<EQBand, EQProcessor::NumBands> bands;
};

static const std::array<EQPreset, 3> kBuiltInPresets = {{
    {
        "Vocal Presence",
        {{
            { EQBandType::HighPass,    80.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::LowShelf,   200.0f, -2.0f, 0.707f, true, 2 },
            { EQBandType::Peak,       300.0f, -1.5f, 0.900f, true, 2 },
            { EQBandType::Peak,       800.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::Peak,      2500.0f, +3.0f, 1.200f, true, 2 },
            { EQBandType::Peak,      4000.0f, +2.5f, 1.000f, true, 2 },
            { EQBandType::HighShelf,10000.0f, +2.0f, 0.707f, true, 2 },
            { EQBandType::LowPass,  20000.0f,  0.0f, 0.707f, true, 2 },
        }}
    },
    {
        "Low End Boost",
        {{
            { EQBandType::HighPass,    30.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::LowShelf,   100.0f, +4.0f, 0.707f, true, 2 },
            { EQBandType::Peak,        80.0f, +3.0f, 1.000f, true, 2 },
            { EQBandType::Peak,       250.0f, +1.5f, 0.707f, true, 2 },
            { EQBandType::Peak,       500.0f, -1.0f, 0.707f, true, 2 },
            { EQBandType::Peak,      5000.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::HighShelf,10000.0f, -1.0f, 0.707f, true, 2 },
            { EQBandType::LowPass,  20000.0f,  0.0f, 0.707f, true, 2 },
        }}
    },
    {
        "Air",
        {{
            { EQBandType::HighPass,    40.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::LowShelf,   100.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::Peak,       250.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::Peak,       800.0f,  0.0f, 0.707f, true, 2 },
            { EQBandType::Peak,      3000.0f, +1.0f, 0.707f, true, 2 },
            { EQBandType::Peak,      7000.0f, +2.0f, 0.707f, true, 2 },
            { EQBandType::HighShelf,12000.0f, +4.0f, 0.707f, true, 2 },
            { EQBandType::LowPass,  20000.0f,  0.0f, 0.707f, true, 2 },
        }}
    }
}};

class EQEditor : public juce::AudioProcessorEditor
{
public:
    EQEditor (EQProcessor& proc_)
        : juce::AudioProcessorEditor (proc_)
        , proc (proc_)
        , eqComponent (proc_)
    {
        addAndMakeVisible (eqComponent);

        // ---- Preset bar ----
        presetCombo.setTextWhenNothingSelected ("-- Presets --");
        presetCombo.setTextWhenNoChoicesAvailable ("No presets");
        presetCombo.setColour (juce::ComboBox::backgroundColourId,  juce::Colour (0xff1e1e1e));
        presetCombo.setColour (juce::ComboBox::outlineColourId,      juce::Colour (0xff444444));
        presetCombo.setColour (juce::ComboBox::textColourId,         juce::Colour (0xffcccccc));
        presetCombo.setColour (juce::ComboBox::arrowColourId,        juce::Colour (0xff888888));
        presetCombo.onChange = [this] { onPresetSelected(); };
        addAndMakeVisible (presetCombo);

        saveButton.setButtonText ("Save Preset");
        saveButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
        saveButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff88cc88));
        saveButton.onClick = [this] { onSavePreset(); };
        addAndMakeVisible (saveButton);

        rebuildPresetCombo();

        setSize (700, 350 + kPresetBarH);
    }

    void resized() override
    {
        const int w = getWidth();
        const int eqH = getHeight() - kPresetBarH;
        eqComponent.setBounds (0, 0, w, eqH);

        // Preset bar: combo takes the left 3/4, save button the rest
        const int barY = eqH;
        const int saveW = 100;
        presetCombo.setBounds (4, barY + 4, w - saveW - 12, kPresetBarH - 8);
        saveButton.setBounds  (w - saveW - 4, barY + 4, saveW, kPresetBarH - 8);
    }

private:
    static constexpr int kPresetBarH = 28;

    EQProcessor&    proc;
    EQComponent     eqComponent;
    juce::ComboBox  presetCombo;
    juce::TextButton saveButton;

    // User presets loaded from / saved to disk
    std::vector<EQPreset> userPresets;

    juce::File getPresetsFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Aizana")
                   .getChildFile ("eq_presets.xml");
    }

    void rebuildPresetCombo()
    {
        presetCombo.clear (juce::dontSendNotification);

        // Built-in presets: IDs 1 … kBuiltInPresets.size()
        for (int i = 0; i < (int)kBuiltInPresets.size(); ++i)
            presetCombo.addItem (kBuiltInPresets[(size_t)i].name, i + 1);

        // User presets: IDs starting at 1000
        loadUserPresets();
        if (!userPresets.empty())
        {
            presetCombo.addSeparator();
            for (int i = 0; i < (int)userPresets.size(); ++i)
                presetCombo.addItem (userPresets[(size_t)i].name, 1000 + i);
        }
    }

    void loadUserPresets()
    {
        userPresets.clear();
        const juce::File f = getPresetsFile();
        if (!f.existsAsFile()) return;

        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || xml->getTagName() != "UserEQPresets") return;

        for (auto* presetXml : xml->getChildIterator())
        {
            EQPreset p;
            p.name = presetXml->getStringAttribute ("name");
            int bi = 0;
            for (auto* bandXml : presetXml->getChildIterator())
            {
                if (bi >= EQProcessor::NumBands) break;
                EQBand& b = p.bands[(size_t)bi++];
                b.type        = (EQBandType) bandXml->getIntAttribute    ("type",    (int) EQBandType::Peak);
                b.frequencyHz = (float)      bandXml->getDoubleAttribute ("freq",    1000.0);
                b.gainDB      = (float)      bandXml->getDoubleAttribute ("gain",    0.0);
                b.Q           = (float)      bandXml->getDoubleAttribute ("q",       0.707);
                b.enabled     =              bandXml->getIntAttribute    ("enabled", 1) != 0;
                b.slopeOrder  =              bandXml->getIntAttribute    ("slope",   2);
            }
            if (!p.name.isEmpty())
                userPresets.push_back (p);
        }
    }

    void saveUserPresetsToFile()
    {
        const juce::File f = getPresetsFile();
        f.getParentDirectory().createDirectory();

        juce::XmlElement xml ("UserEQPresets");
        for (auto& p : userPresets)
        {
            auto* presetXml = xml.createNewChildElement ("Preset");
            presetXml->setAttribute ("name", p.name);
            for (int i = 0; i < EQProcessor::NumBands; ++i)
            {
                const EQBand& b = p.bands[(size_t)i];
                auto* bandXml = presetXml->createNewChildElement ("Band");
                bandXml->setAttribute ("type",    (int)b.type);
                bandXml->setAttribute ("freq",    (double)b.frequencyHz);
                bandXml->setAttribute ("gain",    (double)b.gainDB);
                bandXml->setAttribute ("q",       (double)b.Q);
                bandXml->setAttribute ("enabled", b.enabled ? 1 : 0);
                bandXml->setAttribute ("slope",   b.slopeOrder);
            }
        }
        xml.writeTo (f);
    }

    void applyPreset (const EQPreset& preset)
    {
        for (int i = 0; i < EQProcessor::NumBands; ++i)
            proc.setBand (i, preset.bands[(size_t)i]);
        eqComponent.repaint();
    }

    void onPresetSelected()
    {
        const int id = presetCombo.getSelectedId();
        if (id <= 0) return;

        if (id < 1000)
        {
            // Built-in preset
            const int idx = id - 1;
            if (idx >= 0 && idx < (int)kBuiltInPresets.size())
                applyPreset (kBuiltInPresets[(size_t)idx]);
        }
        else
        {
            // User preset
            const int idx = id - 1000;
            if (idx >= 0 && idx < (int)userPresets.size())
                applyPreset (userPresets[(size_t)idx]);
        }
    }

    void onSavePreset()
    {
        auto dialog = std::make_shared<juce::AlertWindow> (
            "Save EQ Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor ("name", "", "Preset name:");
        dialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        dialog->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, dialog] (int result)
            {
                if (result != 1) return;
                const juce::String name = dialog->getTextEditorContents ("name").trim();
                if (name.isEmpty()) return;

                EQPreset p;
                p.name = name;
                for (int i = 0; i < EQProcessor::NumBands; ++i)
                    p.bands[(size_t)i] = proc.getBand (i);

                // Replace if same name exists, otherwise append
                bool replaced = false;
                for (auto& existing : userPresets)
                {
                    if (existing.name == name) { existing = p; replaced = true; break; }
                }
                if (!replaced) userPresets.push_back (p);

                saveUserPresetsToFile();
                rebuildPresetCombo();
            }));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQEditor)
};

// ---------------------------------------------------------------------------

EQProcessor::EQProcessor()
    : juce::AudioPluginInstance (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Default: 8 bands spread across the audible spectrum, all flat (0 dB), all enabled.
    // The EQ is transparent at 0 dB — enabling bands doesn't change the sound.
    // Frequencies match professional DAW defaults (Logic Pro / Pro-Q style).
    bands[0] = { EQBandType::HighPass,    40.0f, 0.0f, 0.707f, true };
    bands[1] = { EQBandType::LowShelf,   100.0f, 0.0f, 0.707f, true };
    bands[2] = { EQBandType::Peak,       250.0f, 0.0f, 0.707f, true };
    bands[3] = { EQBandType::Peak,       800.0f, 0.0f, 0.707f, true };
    bands[4] = { EQBandType::Peak,      2500.0f, 0.0f, 0.707f, true };
    bands[5] = { EQBandType::Peak,      5000.0f, 0.0f, 0.707f, true };
    bands[6] = { EQBandType::HighShelf, 10000.0f, 0.0f, 0.707f, true };
    bands[7] = { EQBandType::LowPass,   20000.0f, 0.0f, 0.707f, true };

    createBandParameters();
}

// ---------------------------------------------------------------------------
// Automatisierbare Parameter
// ---------------------------------------------------------------------------

void EQProcessor::createBandParameters()
{
    for (int i = 0; i < NumBands; ++i)
    {
        const auto nr     = juce::String (i + 1);
        const auto suffix = " " + nr;                    // "Band 3 Gain" liest sich besser als "b2gain"
        const auto id     = "b" + juce::String (i);

        // Frequenz logarithmisch (skew auf 1 kHz): sonst liegt die halbe
        // Reglerstrecke ueber 10 kHz und die musikalisch wichtigen Bereiche
        // kleben am linken Anschlag — dasselbe gilt fuer Automationskurven.
        juce::NormalisableRange<float> freqRange { 20.0f, 20000.0f };
        freqRange.setSkewForCentre (1000.0f);

        auto* on = new juce::AudioParameterBool  ({ id + "on",   1 }, "Band" + suffix + " An",  bands[i].enabled);
        auto* fr = new juce::AudioParameterFloat ({ id + "freq", 1 }, "Band" + suffix + " Frequenz",
                                                  freqRange, bands[i].frequencyHz);
        auto* gn = new juce::AudioParameterFloat ({ id + "gain", 1 }, "Band" + suffix + " Gain",
                                                  juce::NormalisableRange<float> { -24.0f, 24.0f }, bands[i].gainDB);
        auto* qq = new juce::AudioParameterFloat ({ id + "q",    1 }, "Band" + suffix + " Q",
                                                  juce::NormalisableRange<float> { 0.1f, 18.0f }, bands[i].Q);

        // AudioPluginInstance verlangt HostedParameter (addParameter ist dort privat).
        // AudioParameterFloat/Bool erfuellen das ueber AudioProcessorParameterWithID.
        addHostedParameter (std::unique_ptr<juce::AudioParameterBool>  (on));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (fr));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (gn));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (qq));

        bandParams[i] = { on, fr, gn, qq };
    }
}

void EQProcessor::writeCoefficients (juce::dsp::IIR::Coefficients<float>& dest,
                                     const std::array<float, 6>& src)
{
    // ArrayCoefficients liefert SECHS Werte {b0,b1,b2,a0,a1,a2}; JUCE speichert
    // intern aber nur FUENF — a0 wird herausgeteilt (assignImpl in
    // juce_IIRFilter_impl.h). Dieselbe Normalisierung hier von Hand, sonst passt
    // die Groesse nicht und es wuerde gar nichts geschrieben.
    //
    // (Genau das ist im ersten Anlauf passiert: die Groessenpruefung 5 != 6 hat
    // stillschweigend abgebrochen, der Filter blieb auf dem alten Wert stehen und
    // die Automation wirkte scheinbar nur in eine Richtung. Gefangen hat es der
    // Vergleich gegen JUCEs Referenzformeln.)
    if (dest.coefficients.size() != 5)
        return;

    const float a0 = src[3];

    if (juce::approximatelyEqual (a0, 0.0f))
        return;

    const float inv = 1.0f / a0;
    auto* raw = dest.coefficients.getRawDataPointer();

    raw[0] = src[0] * inv;   // b0
    raw[1] = src[1] * inv;   // b1
    raw[2] = src[2] * inv;   // b2
    raw[3] = src[4] * inv;   // a1
    raw[4] = src[5] * inv;   // a2
}

std::array<float, 6> EQProcessor::coefficientsForBand (const EQBand& b, double sampleRate)
{
    using AC = juce::dsp::IIR::ArrayCoefficients<float>;

    const float freq = juce::jlimit (20.0f, (float) (sampleRate * 0.499), b.frequencyHz);
    const float q    = juce::jmax (0.1f, b.Q);
    const float gain = std::pow (10.0f, b.gainDB / 20.0f);

    // Ausgeschaltet = neutraler Peak (0 dB), exakt wie der bisherige Weg.
    if (! b.enabled)
        return AC::makePeakFilter (sampleRate, freq, q, 1.0f);

    switch (b.type)
    {
        case EQBandType::LowShelf:  return AC::makeLowShelf  (sampleRate, freq, q, gain);
        case EQBandType::HighShelf: return AC::makeHighShelf (sampleRate, freq, q, gain);
        case EQBandType::LowPass:   return AC::makeLowPass   (sampleRate, freq, q);
        case EQBandType::HighPass:  return AC::makeHighPass  (sampleRate, freq, q);
        case EQBandType::Notch:     return AC::makeNotch     (sampleRate, freq, q);
        case EQBandType::Peak:
        default:                    return AC::makePeakFilter (sampleRate, freq, q, gain);
    }
}

void EQProcessor::refreshCoefficientsFromParameters()
{
    const double sr = sampleRateForAudio.load (std::memory_order_relaxed);

    if (sr <= 0.0)
        return;

    for (int i = 0; i < NumBands; ++i)
    {
        const auto& p = bandParams[i];

        if (p.freq == nullptr)
            continue;

        EQBand b   = bands[i];          // Typ und Flanke kommen weiter aus dem State
        b.enabled  = p.on->get();
        b.frequencyHz = p.freq->get();
        b.gainDB      = p.gain->get();
        b.Q           = p.q->get();

        auto& a = applied[i];

        // Nur rechnen, wenn sich etwas bewegt hat. Ohne Automation ist das der
        // Normalfall und der Block kostet dann gar nichts.
        if (a.valid
            && a.on    == b.enabled
            && a.freq  == b.frequencyHz
            && a.gain  == b.gainDB
            && a.q     == b.Q
            && a.type  == (int) b.type
            && a.slope == b.slopeOrder)
            continue;

        const auto vals = coefficientsForBand (b, sr);

        // Die Filterkette und die Kaskade teilen sich die Werte.
        switch (i)
        {
            case 0: writeCoefficients (*eqChain.get<0>().state, vals); break;
            case 1: writeCoefficients (*eqChain.get<1>().state, vals); break;
            case 2: writeCoefficients (*eqChain.get<2>().state, vals); break;
            case 3: writeCoefficients (*eqChain.get<3>().state, vals); break;
            case 4: writeCoefficients (*eqChain.get<4>().state, vals); break;
            case 5: writeCoefficients (*eqChain.get<5>().state, vals); break;
            case 6: writeCoefficients (*eqChain.get<6>().state, vals); break;
            case 7: writeCoefficients (*eqChain.get<7>().state, vals); break;
            default: break;
        }

        writeCoefficients (*cascadeFilters[i].state, vals);
        cascadeActive[i] = b.enabled
                           && (b.type == EQBandType::LowPass || b.type == EQBandType::HighPass)
                           && b.slopeOrder >= 4;

        // Spiegel fuer getBand()/GUI: die Parameter sind die Quelle der Wahrheit,
        // bands[] traegt zusaetzlich Typ und Flanke.
        bands[i].enabled     = b.enabled;
        bands[i].frequencyHz = b.frequencyHz;
        bands[i].gainDB      = b.gainDB;
        bands[i].Q           = b.Q;

        a = { b.frequencyHz, b.gainDB, b.Q, (int) b.type, b.slopeOrder, b.enabled, true };
    }
}

void EQProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;
    sampleRateForAudio.store (sampleRate, std::memory_order_relaxed);

    for (auto& a : applied)
        a.valid = false;   // erzwingt eine Neuberechnung im ersten Block

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(maximumExpectedSamplesPerBlock);
    spec.numChannels      = 2;

    eqChain.prepare(spec);

    // Prepare cascade filters (used for HP/LP slopeOrder=4)
    for (int i = 0; i < NumBands; ++i)
        cascadeFilters[i].prepare(spec);

    // Apply all current band coefficients
    for (int i = 0; i < NumBands; ++i)
        updateBandCoefficients(i);

    // Prepare spectrum FIFO buffer (no reallocation needed if already correct size)
    spectrumBuffer.setSize(1, SpectrumFifoSize, false, true, false);
}

void EQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // -----------------------------------------------------------------------
    // Audio thread — zero dynamic allocations in this function.
    // -----------------------------------------------------------------------
    juce::ScopedNoDenormals noDenormals;

    // Automation/GUI koennen die Parameter jederzeit veraendert haben. Alloc-frei,
    // und rechnet nur, wenn sich wirklich etwas bewegt hat.
    refreshCoefficientsFromParameters();

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    eqChain.process(ctx);

    // Additional passes for HP/LP bands with slopeOrder=4 (24dB/oct = 2× 2nd-order)
    for (int i = 0; i < NumBands; ++i)
    {
        if (cascadeActive[i])
        {
            juce::dsp::AudioBlock<float> cascadeBlock (buffer);
            juce::dsp::ProcessContextReplacing<float> cascadeCtx (cascadeBlock);
            cascadeFilters[i].process (cascadeCtx);
        }
    }

    // Push samples into spectrum FIFO (lock-free)
    // Message thread reads this in a timer callback to run the FFT.
    const int numSamples = buffer.getNumSamples();
    int start1, size1, start2, size2;
    spectrumFifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    if (size1 > 0)
        spectrumBuffer.copyFrom(0, start1, buffer, 0, 0, size1);
    if (size2 > 0)
        spectrumBuffer.copyFrom(0, start2, buffer, 0, size1, size2);

    spectrumFifo.finishedWrite(size1 + size2);
}

void EQProcessor::setBand(int bandIndex, const EQBand& band)
{
    jassert(bandIndex >= 0 && bandIndex < NumBands);
    bands[bandIndex] = band;

    // Die vier automatisierbaren Werte in die Parameter spiegeln, sonst wuesste
    // weder der Host noch eine laufende Automation etwas von der Aenderung (und
    // der naechste Block wuerde den alten Parameterwert zurueckschreiben).
    // Laeuft nur vom Message-Thread: GUI, Presets, setStateInformation.
    if (auto& p = bandParams[bandIndex]; p.freq != nullptr)
    {
        *p.on   = band.enabled;
        *p.freq = band.frequencyHz;
        *p.gain = band.gainDB;
        *p.q    = band.Q;
    }

    updateBandCoefficients(bandIndex);
}

EQBand EQProcessor::getBand(int bandIndex) const
{
    jassert(bandIndex >= 0 && bandIndex < NumBands);

    EQBand b = bands[bandIndex];   // Typ und Flankensteilheit leben weiter hier

    // Frequenz/Gain/Q/An aus den Parametern lesen, damit die Kurve im Editor
    // auch dann stimmt, wenn eine Automation sie gerade bewegt.
    if (const auto& p = bandParams[bandIndex]; p.freq != nullptr)
    {
        b.enabled     = p.on->get();
        b.frequencyHz = p.freq->get();
        b.gainDB      = p.gain->get();
        b.Q           = p.q->get();
    }

    return b;
}

bool EQProcessor::getSpectrumData(std::vector<float>& magnitudesOut, int fftSize)
{
    // Message-thread only. Reads from spectrum FIFO.
    int available = spectrumFifo.getNumReady();
    if (available < fftSize)
        return false;

    int start1, size1, start2, size2;
    spectrumFifo.prepareToRead(fftSize, start1, size1, start2, size2);

    // Copy to user buffer (this is the message thread — allocation is fine here)
    magnitudesOut.resize(static_cast<size_t>(fftSize));
    const float* fifoData = spectrumBuffer.getReadPointer(0);
    std::copy(fifoData + start1, fifoData + start1 + size1, magnitudesOut.begin());
    if (size2 > 0)
        std::copy(fifoData + start2, fifoData + start2 + size2,
                  magnitudesOut.begin() + size1);

    spectrumFifo.finishedRead(size1 + size2);
    return true;
}

void EQProcessor::updateBandCoefficients(int bandIndex)
{
    if (currentSampleRate <= 0.0)
        return;

    const EQBand& b = bands[bandIndex];

    juce::dsp::IIR::Coefficients<float>::Ptr coeffs;

    float freq = juce::jlimit(20.0f, (float)(currentSampleRate * 0.499), b.frequencyHz);
    float Q    = juce::jmax(0.1f, b.Q);
    float gain = std::pow(10.0f, b.gainDB / 20.0f);

    if (!b.enabled)
    {
        // Pass-through: unity peak at band frequency (0 dB, transparent)
        coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, freq, Q, 1.0f);
        cascadeActive[bandIndex] = false;
    }
    else
    {
        switch (b.type)
        {
            case EQBandType::LowShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                    currentSampleRate, freq, Q, gain);
                cascadeActive[bandIndex] = false;
                break;

            case EQBandType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                    currentSampleRate, freq, Q, gain);
                cascadeActive[bandIndex] = false;
                break;

            case EQBandType::LowPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
                    currentSampleRate, freq, Q);
                cascadeActive[bandIndex] = (b.slopeOrder >= 4);
                break;

            case EQBandType::HighPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
                    currentSampleRate, freq, Q);
                cascadeActive[bandIndex] = (b.slopeOrder >= 4);
                break;

            case EQBandType::Notch:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeNotch(
                    currentSampleRate, freq, Q);
                cascadeActive[bandIndex] = false;
                break;

            case EQBandType::Peak:
            default:
                if (std::abs(b.gainDB) < 1e-4f)
                    coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        currentSampleRate, freq, Q, 1.0f);
                else
                    coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        currentSampleRate, freq, Q, gain);
                cascadeActive[bandIndex] = false;
                break;
        }
    }

    applyCoefficients(bandIndex, coeffs);
}

void EQProcessor::applyCoefficients(int bandIndex,
    juce::dsp::IIR::Coefficients<float>::Ptr coeffs)
{
    // ProcessorDuplicator exposes a shared `state` pointer (same object for L and R
    // channels).  Copying INTO the existing state object via `*state = *coeffs` keeps
    // the same Coefficients instance alive on the audio thread while updating values.
    // This is the JUCE-recommended pattern for live coefficient updates with
    // ProcessorDuplicator — see ProcessorDuplicator.h and JUCE DSP tutorials.
    switch (bandIndex)
    {
        case 0: *eqChain.get<0>().state = *coeffs; break;
        case 1: *eqChain.get<1>().state = *coeffs; break;
        case 2: *eqChain.get<2>().state = *coeffs; break;
        case 3: *eqChain.get<3>().state = *coeffs; break;
        case 4: *eqChain.get<4>().state = *coeffs; break;
        case 5: *eqChain.get<5>().state = *coeffs; break;
        case 6: *eqChain.get<6>().state = *coeffs; break;
        case 7: *eqChain.get<7>().state = *coeffs; break;
        default: jassertfalse; break;
    }

    // Mirror coefficients to cascade filter (used when slopeOrder=4 for HP/LP)
    *cascadeFilters[bandIndex].state = *coeffs;
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void EQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("EQState");

    for (int i = 0; i < NumBands; ++i)
    {
        const EQBand& b = bands[i];
        auto* bandXml = xml.createNewChildElement ("Band" + juce::String (i));
        bandXml->setAttribute ("type",    (int) b.type);
        bandXml->setAttribute ("freq",    (double) b.frequencyHz);
        bandXml->setAttribute ("gain",    (double) b.gainDB);
        bandXml->setAttribute ("q",       (double) b.Q);
        bandXml->setAttribute ("enabled", b.enabled ? 1 : 0);
        bandXml->setAttribute ("slope",   b.slopeOrder);
    }

    copyXmlToBinary (xml, destData);
}

void EQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("EQState"))
        return;

    for (int i = 0; i < NumBands; ++i)
    {
        auto* bandXml = xml->getChildByName ("Band" + juce::String (i));
        if (bandXml == nullptr)
            continue;

        EQBand b;
        b.type        = (EQBandType) bandXml->getIntAttribute  ("type",    (int) EQBandType::Peak);
        b.frequencyHz = (float)      bandXml->getDoubleAttribute ("freq",   1000.0);
        b.gainDB      = (float)      bandXml->getDoubleAttribute ("gain",   0.0);
        b.Q           = (float)      bandXml->getDoubleAttribute ("q",      0.707);
        b.enabled     =              bandXml->getIntAttribute    ("enabled", 0) != 0;
        b.slopeOrder  =              bandXml->getIntAttribute    ("slope",   2);
        setBand (i, b);
    }
}

// ---------------------------------------------------------------------------
// AudioPluginInstance interface
// ---------------------------------------------------------------------------

void EQProcessor::fillInPluginDescription (juce::PluginDescription& desc) const
{
    desc = BuiltInPluginFormat::getDescriptionFor ("Aizana EQ");
}

juce::AudioProcessorEditor* EQProcessor::createEditor()
{
    return new EQEditor (*this);
}
