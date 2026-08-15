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

class EQEditor : public juce::AudioProcessorEditor,
                 private juce::Timer
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
        buildBandBar();
        buildDynamicsBar();

        // Wer ein Band auf der Kurve anfasst, meint dieses Band — die Leiste
        // folgt ihm, damit Solo/Mute ohne Umweg ueber die Bandwahl erreichbar
        // sind. Das ist der Arbeitsablauf aus Studio One: Band anklicken, Solo
        // druecken, Stoerstelle suchen.
        eqComponent.onBandSelected = [this] (int band) { selectBand (band); };

        setSize (700, 350 + kPresetBarH + kBandBarH + kDynBarH);
        startTimerHz (10);
    }

    ~EQEditor() override
    {
        stopTimer();

        // Solo ist an das Hinsehen gebunden. Bliebe es nach dem Schliessen des
        // Fensters an, klaenge die Spur duenn und niemand saehe warum — die
        // teuerste Sorte Fehler, weil sie nach einem kaputten Mix aussieht.
        proc.setSoloedBand (-1);
    }

    void resized() override
    {
        const int w = getWidth();
        const int eqH = getHeight() - kPresetBarH - kBandBarH - kDynBarH;
        eqComponent.setBounds (0, 0, w, eqH);

        // Preset bar: combo takes the left 3/4, save button the rest
        const int barY = eqH;
        const int saveW = 100;
        presetCombo.setBounds (4, barY + 4, w - saveW - 12, kPresetBarH - 8);
        saveButton.setBounds  (w - saveW - 4, barY + 4, saveW, kPresetBarH - 8);

        // ---- Band-Leiste (Bandwahl + Solo + Mute) ----
        auto br = juce::Rectangle<int> (0, barY + kPresetBarH, w, kBandBarH).reduced (4, 3);
        auto bcell = [&br] (int width) { auto c = br.removeFromLeft (width); br.removeFromLeft (4); return c; };

        bandCombo  .setBounds (bcell (80));
        soloButton .setBounds (bcell (62));
        muteButton .setBounds (bcell (62));

        // F5: Werte eintippen. Beschriftung + Feld als Paar, damit klar ist,
        // welche Zahl wohin gehoert.
        freqCaption.setBounds (bcell (34));
        freqField  .setBounds (bcell (86));
        gainCaption.setBounds (bcell (36));
        gainField  .setBounds (bcell (78));
        qCaption   .setBounds (bcell (18));
        qField     .setBounds (bcell (58));
        soloHintLabel.setBounds (br);

        // ---- Dynamik-Leiste ----
        auto r = juce::Rectangle<int> (0, barY + kPresetBarH + kBandBarH, w, kDynBarH).reduced (4, 3);
        auto cell = [&r] (int width) { auto c = r.removeFromLeft (width); r.removeFromLeft (4); return c; };

        dynOnButton .setBounds  (cell (84));
        threshSlider.setBounds  (cell (140));
        ratioSlider .setBounds  (cell (110));
        attackSlider.setBounds  (cell (px (r.getWidth() / 2 - 2)));
        releaseSlider.setBounds (r);
    }

private:
    static int px (int v) { return juce::jmax (60, v); }

    static constexpr int kPresetBarH = 28;
    static constexpr int kBandBarH   = 28;
    static constexpr int kDynBarH    = 30;

    EQProcessor&    proc;
    EQComponent     eqComponent;
    juce::ComboBox  presetCombo;
    juce::TextButton saveButton;

    //==========================================================================
    // Dynamik-Leiste
    //
    // Ohne Bedienelemente waere die Dynamik nur ueber Automation erreichbar — fuer
    // einen Mix-Engineer also praktisch nicht vorhanden. Bewusst EINE Zeile mit
    // Bandwahl statt acht Zeilen: der Platz gehoert der Kurve.
    //==========================================================================
    //==========================================================================
    // Band-Leiste — Bandwahl, Solo, Mute
    //
    // Solo und Mute sind Grundausstattung jedes Profi-EQs; ohne sie laesst sich
    // eine Stoerfrequenz nicht heraushoeren und ein einzelnes Band nicht
    // gegenpruefen. Bewusst EINE geteilte Bandwahl fuer Band- und
    // Dynamik-Leiste: zwei Bandwaehler nebeneinander waeren eine Fehlerquelle.
    //==========================================================================
    juce::ComboBox     bandCombo;
    juce::TextButton   soloButton;
    juce::TextButton   muteButton;
    juce::Label        soloHintLabel;

    //==========================================================================
    // F5 — Werte eintippen (User-Befund 2026-08-15)
    //
    // "Man kann das auch eintippen von Hand" — und zwar aus einem konkreten
    // Grund: wer die Frequenz schon gefunden hat, will sie beim Einstellen des
    // Gains NICHT versehentlich mitverschieben. Ziehen aendert beides zugleich;
    // ein Zahlenfeld aendert genau eine Groesse. Jeder uebliche EQ hat das.
    //
    // Editierbare Labels statt Slider: ein Slider mit Textfeld nimmt mehr Platz
    // und laedt zum Ziehen ein — hier geht es ausdruecklich um das Gegenteil.
    //==========================================================================
    juce::Label freqCaption, gainCaption, qCaption;
    juce::Label freqField,   gainField,   qField;

    juce::ToggleButton dynOnButton;
    juce::Slider       threshSlider  { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    juce::Slider       ratioSlider   { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    juce::Slider       attackSlider  { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    juce::Slider       releaseSlider { juce::Slider::LinearBar, juce::Slider::TextBoxRight };

    int selectedDynBand = 0;

    void buildBandBar()
    {
        for (int i = 0; i < EQProcessor::NumBands; ++i)
            bandCombo.addItem ("Band " + juce::String (i + 1), i + 1);

        bandCombo.setSelectedId (1, juce::dontSendNotification);
        bandCombo.setTitle ("Bandwahl");
        bandCombo.onChange = [this]
        {
            selectedDynBand = juce::jlimit (0, EQProcessor::NumBands - 1,
                                            bandCombo.getSelectedId() - 1);
            refreshBandBarFromProcessor();
            refreshDynamicsFromProcessor();
        };
        addAndMakeVisible (bandCombo);

        soloButton.setButtonText ("Solo");
        soloButton.setTitle ("Band Solo");
        soloButton.setTooltip (juce::String::fromUTF8 (
            "H\xc3\xb6rt NUR den Frequenzbereich dieses Bandes (zum Aufsp\xc3\xbc""ren einer "
            "St\xc3\xb6rstelle). Bei einem Low/High Cut h\xc3\xb6rt man, was weggeschnitten wird."));
        soloButton.setClickingTogglesState (false);   // I-04
        soloButton.onClick = [this]
        {
            const bool nowOn = proc.getSoloedBand() != selectedDynBand;
            proc.setSoloedBand (nowOn ? selectedDynBand : -1);
            refreshBandBarFromProcessor();
        };
        addAndMakeVisible (soloButton);

        muteButton.setButtonText ("Mute");
        muteButton.setTitle ("Band Mute");
        muteButton.setTooltip (juce::String::fromUTF8 (
            "Schaltet dieses eine Band ab \xe2\x80\x94 f\xc3\xbcr den A/B-Vergleich, "
            "w\xc3\xa4hrend alle anderen B\xc3\xa4nder weiterarbeiten."));
        muteButton.setClickingTogglesState (false);   // I-04
        muteButton.onClick = [this]
        {
            proc.setBandEnabled (selectedDynBand, ! proc.isBandEnabled (selectedDynBand));
            refreshBandBarFromProcessor();
        };
        addAndMakeVisible (muteButton);

        auto setUpCaption = [this] (juce::Label& l, const juce::String& text)
        {
            l.setFont (juce::FontOptions (11.0f));
            l.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
            l.setText (text, juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        };

        auto setUpField = [this] (juce::Label& l, const juce::String& name,
                                  std::function<void()> onCommit)
        {
            l.setFont (juce::FontOptions (12.0f));
            l.setColour (juce::Label::textColourId,       juce::Colour (0xffdddddd));
            l.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1e1e1e));
            l.setColour (juce::Label::outlineColourId,    juce::Colour (0xff444444));
            l.setJustificationType (juce::Justification::centred);
            l.setEditable (true);          // Einfachklick genuegt — kein Doppelklick suchen
            l.setTitle (name);
            l.onTextChange = std::move (onCommit);
            addAndMakeVisible (l);
        };

        setUpCaption (freqCaption, "Freq");
        setUpCaption (gainCaption, "Gain");
        setUpCaption (qCaption,    "Q");

        setUpField (freqField, "Band Frequenz", [this] { commitFreqField(); });
        setUpField (gainField, "Band Gain",     [this] { commitGainField(); });
        setUpField (qField,    "Band Q",        [this] { commitQField(); });

        soloHintLabel.setFont (juce::FontOptions (11.0f));
        soloHintLabel.setColour (juce::Label::textColourId, juce::Colour (0xff888888));
        // Zustandsanzeige statt Hinweistext: sie ist fuer den Anwender ein
        // Ableseort und traegt denselben Text im NAMEN — damit ist der Zustand
        // des Bandes auch ohne Bildschirm pruefbar (Hausregel aus den
        // GUI-Testlaeufen: die Farbe allein beweist nichts).
        soloHintLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (soloHintLabel);

        refreshBandBarFromProcessor();
    }

    /** Band von aussen waehlen (Klick auf einen Griff in der Kurve). */
    void selectBand (int band)
    {
        const int b = juce::jlimit (0, EQProcessor::NumBands - 1, band);
        if (b == selectedDynBand)
            return;

        selectedDynBand = b;
        bandCombo.setSelectedId (b + 1, juce::dontSendNotification);
        refreshBandBarFromProcessor();
        refreshDynamicsFromProcessor();
    }

    //--------------------------------------------------------------------------
    // F5 — Eingaben uebernehmen
    //
    // Nachsichtig lesen: "1k", "1 kHz", "1000" und "-4,8" (deutsches Komma)
    // sollen alle funktionieren. Ein Feld, das die eigene Anzeige nicht wieder
    // annimmt, ist eine Falle — genau deshalb wird auch mit demselben Format
    // zurueckgeschrieben, in dem gelesen wird.
    //--------------------------------------------------------------------------
    static double parseNumber (juce::String text, bool& ok)
    {
        text = text.trim().replaceCharacter (',', '.');

        double factor = 1.0;
        if (text.endsWithIgnoreCase ("khz") || text.endsWithIgnoreCase ("k"))
            factor = 1000.0;

        // Alles ausser Ziffern, Punkt und Vorzeichen entfernen ("1.2 kHz" -> "1.2")
        juce::String digits;
        for (auto c : text)
            if (juce::CharacterFunctions::isDigit (c) || c == '.' || c == '-' || c == '+')
                digits << c;

        ok = digits.containsAnyOf ("0123456789");
        return digits.getDoubleValue() * factor;
    }

    void commitFreqField()
    {
        bool ok = false;
        const double hz = parseNumber (freqField.getText(), ok);
        if (ok)
        {
            auto b = proc.getBand (selectedDynBand);
            b.frequencyHz = (float) juce::jlimit (20.0, 20000.0, hz);
            proc.setBand (selectedDynBand, b);
        }
        refreshBandFields (true);
    }

    void commitGainField()
    {
        bool ok = false;
        const double db = parseNumber (gainField.getText(), ok);
        if (ok)
        {
            auto b = proc.getBand (selectedDynBand);
            b.gainDB = (float) juce::jlimit (-18.0, 18.0, db);
            proc.setBand (selectedDynBand, b);
        }
        refreshBandFields (true);
    }

    void commitQField()
    {
        bool ok = false;
        const double q = parseNumber (qField.getText(), ok);
        if (ok)
        {
            auto b = proc.getBand (selectedDynBand);
            b.Q = (float) juce::jlimit (0.1, 30.0, q);
            proc.setBand (selectedDynBand, b);
        }
        refreshBandFields (true);
    }

    /** Anzeige nachfuehren. Ein Feld, in dem gerade GETIPPT wird, bleibt in
        Ruhe — sonst kaempft der Timer gegen die Hand des Users (dieselbe Regel
        wie bei den Dynamik-Reglern). */
    void refreshBandFields (bool force)
    {
        const auto b = proc.getBand (selectedDynBand);

        auto put = [force] (juce::Label& l, const juce::String& text)
        {
            if (force || ! l.isBeingEdited())
                l.setText (text, juce::dontSendNotification);
        };

        const auto freqText = b.frequencyHz >= 1000.0f
                                ? juce::String (b.frequencyHz / 1000.0f, 2) + " kHz"
                                : juce::String (b.frequencyHz, 0) + " Hz";
        const auto gainText = juce::String (b.gainDB, 1) + " dB";
        const auto qText    = juce::String (b.Q, 2);

        put (freqField, freqText);
        put (gainField, gainText);
        put (qField,    qText);

        const auto summary = "Band " + juce::String (selectedDynBand + 1) + ": "
                             + freqText + " / " + gainText + " / Q " + qText;
        soloHintLabel.setText (summary, juce::dontSendNotification);
        soloHintLabel.setTitle (summary);
    }

    void refreshBandBarFromProcessor()
    {
        const bool soloOn = proc.getSoloedBand() == selectedDynBand;
        const bool muted  = ! proc.isBandEnabled (selectedDynBand);

        // Zustand IM NAMEN mitfuehren: die Farbe allein ist weder vorlesbar
        // noch maschinell pruefbar (Werkzeug-Regel aus den GUI-Testlaeufen).
        soloButton.setTitle (soloOn ? juce::String ("Band Solo (aktiv)")
                                    : juce::String ("Band Solo"));
        muteButton.setTitle (muted  ? juce::String ("Band Mute (aktiv)")
                                    : juce::String ("Band Mute"));

        soloButton.setColour (juce::TextButton::buttonColourId,
                              soloOn ? juce::Colour (0xffbb9900) : juce::Colour (0xff2a2a2a));
        soloButton.setColour (juce::TextButton::textColourOffId,
                              soloOn ? juce::Colours::black : juce::Colour (0xffcccccc));

        muteButton.setColour (juce::TextButton::buttonColourId,
                              muted ? juce::Colour (0xffaa3333) : juce::Colour (0xff2a2a2a));
        muteButton.setColour (juce::TextButton::textColourOffId,
                              muted ? juce::Colours::white : juce::Colour (0xffcccccc));

        soloButton.repaint();
        muteButton.repaint();

        refreshBandFields (false);
    }

    void buildDynamicsBar()
    {
        dynOnButton.setButtonText ("Dynamik");
        dynOnButton.setTitle ("Dynamik An");
        // Nur der Haken selbst darf ein gemutetes Band wieder einschalten (s. u.).
        dynOnButton.onClick = [this] { pushDynamicsToProcessor (true); };
        addAndMakeVisible (dynOnButton);

        auto setUp = [this] (juce::Slider& s, const juce::String& name,
                             double lo, double hi, const juce::String& suffix)
        {
            s.setRange (lo, hi, 0.1);
            s.setTextValueSuffix (suffix);
            s.setTitle (name);
            s.setTooltip (name);
            s.onValueChange = [this] { pushDynamicsToProcessor (false); };
            addAndMakeVisible (s);
        };

        setUp (threshSlider,  "Dyn-Schwelle", -60.0,   0.0, " dB");
        setUp (ratioSlider,   "Dyn-Ratio",      1.0,  10.0, ":1");
        setUp (attackSlider,  "Dyn-Attack",     0.5, 100.0, " ms");
        setUp (releaseSlider, "Dyn-Release",    5.0,1000.0, " ms");

        refreshDynamicsFromProcessor();
    }

    /** Werte aus dem Prozessor holen — auch waehrend Automation laeuft (Timer). */
    void refreshDynamicsFromProcessor()
    {
        const auto b = proc.getBand (selectedDynBand);

        dynOnButton.setToggleState (b.dynOn, juce::dontSendNotification);

        // Einen Regler, an dem gerade gezogen wird, NICHT ueberschreiben — sonst
        // kaempft der Timer gegen die Hand des Users (I-02-Verwandtschaft).
        if (! threshSlider.isMouseButtonDown())
            threshSlider.setValue (b.dynThresholdDB, juce::dontSendNotification);
        if (! ratioSlider.isMouseButtonDown())
            ratioSlider.setValue (b.dynRatio, juce::dontSendNotification);

        if (! attackSlider.isMouseButtonDown())
            attackSlider.setValue (proc.getDynAttackMs(), juce::dontSendNotification);
        if (! releaseSlider.isMouseButtonDown())
            releaseSlider.setValue (proc.getDynReleaseMs(), juce::dontSendNotification);
    }

    void pushDynamicsToProcessor (bool cameFromDynSwitch)
    {
        auto b = proc.getBand (selectedDynBand);
        b.dynOn          = dynOnButton.getToggleState();
        b.dynThresholdDB = (float) threshSlider.getValue();
        b.dynRatio       = (float) ratioSlider.getValue();

        // Ein ausgeschaltetes Band filtert nicht, also kann es auch nichts
        // dynamisch absenken. Wer den HAKEN setzt, meint das Band — sonst haette
        // er keine Wirkung und niemand saehe, warum.
        //
        // Aber NUR beim Haken: seit es einen Mute-Knopf gibt, wuerde diese Regel
        // sonst zur Falle. Ein Band bewusst stummschalten und danach einen
        // Dynamik-Regler anfassen haette es still wieder eingeschaltet — der
        // A/B-Vergleich waere zerstoert, ohne dass jemand die Ursache saehe.
        if (cameFromDynSwitch && b.dynOn && ! b.enabled)
            b.enabled = true;

        proc.setBand (selectedDynBand, b);

        proc.setDynTimes ((float) attackSlider.getValue(), (float) releaseSlider.getValue());
    }

    void timerCallback() override
    {
        refreshDynamicsFromProcessor();
        refreshBandBarFromProcessor();
    }

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

        // Dynamik: Schwelle und Ratio in denselben Einheiten wie im Kompressor —
        // der User denkt in "ab hier" und "wie stark", nicht in abstrakten Kurven.
        auto* dOn = new juce::AudioParameterBool  ({ id + "dynon", 1 }, "Band" + suffix + " Dynamik",
                                                   bands[i].dynOn);
        auto* dTh = new juce::AudioParameterFloat ({ id + "dynth", 1 }, "Band" + suffix + " Dyn-Schwelle",
                                                   juce::NormalisableRange<float> { -60.0f, 0.0f },
                                                   bands[i].dynThresholdDB);
        auto* dRa = new juce::AudioParameterFloat ({ id + "dynra", 1 }, "Band" + suffix + " Dyn-Ratio",
                                                   juce::NormalisableRange<float> { 1.0f, 10.0f },
                                                   bands[i].dynRatio);

        // AudioPluginInstance verlangt HostedParameter (addParameter ist dort privat).
        // AudioParameterFloat/Bool erfuellen das ueber AudioProcessorParameterWithID.
        addHostedParameter (std::unique_ptr<juce::AudioParameterBool>  (on));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (fr));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (gn));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (qq));
        addHostedParameter (std::unique_ptr<juce::AudioParameterBool>  (dOn));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (dTh));
        addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (dRa));

        bandParams[i] = { on, fr, gn, qq, dOn, dTh, dRa };
    }

    // Attack/Release gelten geraeteweit: ein Regelverhalten fuer alle Baender.
    auto* at = new juce::AudioParameterFloat ({ "dynattack", 1 }, "Dynamik Attack (ms)",
                                              juce::NormalisableRange<float> { 0.5f, 100.0f }, 10.0f);
    auto* rl = new juce::AudioParameterFloat ({ "dynrelease", 1 }, "Dynamik Release (ms)",
                                              juce::NormalisableRange<float> { 5.0f, 1000.0f }, 120.0f);
    addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (at));
    addHostedParameter (std::unique_ptr<juce::AudioParameterFloat> (rl));
    dynAttackMs  = at;
    dynReleaseMs = rl;
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
        b.dynOn          = p.dynOn     != nullptr && p.dynOn->get();
        b.dynThresholdDB = p.dynThresh != nullptr ? p.dynThresh->get() : b.dynThresholdDB;
        b.dynRatio       = p.dynRatio  != nullptr ? p.dynRatio->get()  : b.dynRatio;

        // Dynamik wirkt nur, wo es ueberhaupt einen Band-Gain gibt. Bei HP/LP/Notch
        // gibt es nichts abzusenken, und ein ausgeschaltetes Band filtert gar nicht.
        //
        // WICHTIG: das darf nicht in den Wunsch des Users zurueckgeschrieben werden.
        // Genau daran ist die erste Fassung aufgefallen — der Haken sprang beim
        // Anklicken sofort wieder heraus, weil das Band noch aus war. Der Wunsch
        // bleibt stehen, nur die WIRKUNG haengt am Zustand des Bandes.
        const bool gainBearing = (b.type == EQBandType::Peak
                                  || b.type == EQBandType::LowShelf
                                  || b.type == EQBandType::HighShelf);
        dynEffective[i] = b.dynOn && b.enabled && gainBearing;
        dynActiveForUi[(size_t) i].store (dynEffective[i], std::memory_order_relaxed);
        if (! dynEffective[i])
            dynReductionDb[(size_t) i].store (0.0f, std::memory_order_relaxed);

        bands[i].dynOn          = b.dynOn;
        bands[i].dynThresholdDB = b.dynThresholdDB;
        bands[i].dynRatio       = b.dynRatio;
        b.dynOn                 = dynEffective[i];

        auto& a = applied[i];

        // Nur rechnen, wenn sich etwas bewegt hat. Ohne Automation ist das der
        // Normalfall und der Block kostet dann gar nichts.
        if (a.valid
            && a.on    == b.enabled
            && a.freq  == b.frequencyHz
            && a.gain  == b.gainDB
            && a.q     == b.Q
            && a.type  == (int) b.type
            && a.slope == b.slopeOrder
            && a.dyn   == b.dynOn)
            continue;

        const auto vals = coefficientsForBand (b, sr);

        // Die Filterkette und die Kaskade teilen sich die Werte.
        writeBandCoefficients (i, b.gainDB, sr);
        dyn[i].appliedGain = b.gainDB;

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

        a = { b.frequencyHz, b.gainDB, b.Q, (int) b.type, b.slopeOrder,
              b.enabled, b.dynOn, true };
    }

    // ---- Dynamik-Vorbereitung: Detektor-Filter und Zeitkonstanten ----
    anyDynamicActive = false;

    for (int i = 0; i < NumBands; ++i)
    {
        if (! dynEffective[i])
            continue;

        anyDynamicActive = true;
        auto& d = dyn[i];

        // Der Detektor hoert genau das Band: Bandpass auf Frequenz und Q. Aendert
        // sich einer der beiden (auch per Automation), wird er nachgezogen —
        // wieder ueber ArrayCoefficients, also ohne Allokation.
        if (d.detectorCoeffs != nullptr
            && (d.detFreq != bands[i].frequencyHz || d.detQ != bands[i].Q))
        {
            // Ueber dieselbe Normalisierung wie die Bandfilter — ArrayCoefficients
            // liefert SECHS Werte, JUCE speichert FUENF. Von Hand nachgebaut waere
            // genau die Falle, die beim EQ-Umbau schon einmal zugeschlagen hat
            // (Groessenpruefung schlaegt fehl, es wird still gar nichts geschrieben).
            const auto bp = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass (
                                sr, juce::jlimit (20.0f, (float) (sr * 0.45), bands[i].frequencyHz),
                                juce::jmax (0.1f, bands[i].Q));
            writeCoefficients (*d.detectorCoeffs, bp);

            d.detFreq = bands[i].frequencyHz;
            d.detQ    = bands[i].Q;
        }
    }

    if (anyDynamicActive)
    {
        const float at = dynAttackMs  != nullptr ? dynAttackMs->get()  : 10.0f;
        const float rl = dynReleaseMs != nullptr ? dynReleaseMs->get() : 120.0f;

        if (at != dynCoeffSourceAttack || rl != dynCoeffSourceRelease)
        {
            // Ein Schritt = ein Teilblock. exp() steht damit einmal pro Block statt
            // pro Sample im Weg — und nur, wenn sich Attack/Release bewegt haben.
            const float dt = (float) (kDynSubBlock / sr);
            dynAttackCoeff  = 1.0f - std::exp (-dt / juce::jmax (0.0001f, at * 0.001f));
            dynReleaseCoeff = 1.0f - std::exp (-dt / juce::jmax (0.0001f, rl * 0.001f));
            dynCoeffSourceAttack  = at;
            dynCoeffSourceRelease = rl;
        }
    }
}

void EQProcessor::setDynTimes (float attackMs, float releaseMs)
{
    if (dynAttackMs  != nullptr) *dynAttackMs  = attackMs;
    if (dynReleaseMs != nullptr) *dynReleaseMs = releaseMs;
}

float EQProcessor::getDynAttackMs()  const { return dynAttackMs  != nullptr ? dynAttackMs->get()  : 10.0f; }
float EQProcessor::getDynReleaseMs() const { return dynReleaseMs != nullptr ? dynReleaseMs->get() : 120.0f; }

void EQProcessor::writeBandCoefficients (int bandIndex, float gainDB, double sampleRate)
{
    EQBand b = bands[bandIndex];
    b.gainDB = gainDB;

    const auto vals = coefficientsForBand (b, sampleRate);

    switch (bandIndex)
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
}

//==============================================================================
// Band-Solo (Audition) und Band-Mute
//==============================================================================

EQProcessor::AuditionShape EQProcessor::auditionShapeFor (EQBandType type) noexcept
{
    switch (type)
    {
        // Ein Low Cut (HighPass) nimmt die Tiefen WEG — beim Abhoeren will man
        // genau die hoeren, um zu pruefen, ob man an der richtigen Stelle
        // schneidet. Deshalb ist die Hoerkurve hier die Umkehrung des Filters
        // (Pro-Q macht das genauso). Ein Low Shelf betrifft denselben Bereich.
        case EQBandType::HighPass:
        case EQBandType::LowShelf:  return AuditionShape::lowPass;

        case EQBandType::LowPass:
        case EQBandType::HighShelf: return AuditionShape::highPass;

        case EQBandType::Peak:
        case EQBandType::Notch:
        default:                    return AuditionShape::bandPass;
    }
}

void EQProcessor::setSoloedBand (int bandIndex)
{
    const int v = (bandIndex >= 0 && bandIndex < NumBands) ? bandIndex : -1;
    soloedBand.store (v, std::memory_order_relaxed);

    // Erzwingt das Neuschreiben der Audition-Koeffizienten im naechsten Block —
    // ohne das wuerde ein Wechsel zwischen zwei Baendern mit zufaellig gleicher
    // Frequenz und Q die alte Kurve weiterbenutzen.
    auditionApplied.valid = false;
}

void EQProcessor::setBandEnabled (int bandIndex, bool shouldBeEnabled)
{
    if (bandIndex < 0 || bandIndex >= NumBands)
        return;

    auto b = bands[bandIndex];
    b.enabled = shouldBeEnabled;
    setBand (bandIndex, b);   // spiegelt in den Parameter und rechnet neu
}

float EQProcessor::getDynamicGainReductionDb (int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= NumBands)
        return 0.0f;

    return dynActiveForUi[(size_t) bandIndex].load (std::memory_order_relaxed)
             ? dynReductionDb[(size_t) bandIndex].load (std::memory_order_relaxed)
             : 0.0f;
}

bool EQProcessor::isDynamicsActive (int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= NumBands)
        return false;

    return dynActiveForUi[(size_t) bandIndex].load (std::memory_order_relaxed);
}

bool EQProcessor::isBandEnabled (int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= NumBands)
        return false;

    return bands[bandIndex].enabled;
}

void EQProcessor::refreshAuditionCoefficients (int bandIndex, double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    const auto& b = bands[bandIndex];
    const auto shape = auditionShapeFor (b.type);

    const float freq = juce::jlimit (20.0f, (float) (sampleRate * 0.499), b.frequencyHz);

    // Bandpass folgt dem Q des Bandes (enges Band = enges Hoerfenster, genau wie
    // in der Referenz). Unten begrenzt, damit ein sehr breites Band nicht
    // faktisch das ganze Signal durchlaesst und das Solo wirkungslos aussieht.
    // Tief-/Hochpass laufen auf Butterworth — dort definiert die Eckfrequenz
    // den Bereich, nicht das Q des Bandes.
    const float q = (shape == AuditionShape::bandPass)
                      ? juce::jlimit (0.5f, 30.0f, b.Q)
                      : 0.707f;

    if (auditionApplied.valid
        && auditionApplied.shape == (int) shape
        && juce::approximatelyEqual (auditionApplied.freq, freq)
        && juce::approximatelyEqual (auditionApplied.q, q))
        return;

    using AC = juce::dsp::IIR::ArrayCoefficients<float>;
    const std::array<float, 6> vals =
        shape == AuditionShape::bandPass ? AC::makeBandPass (sampleRate, freq, q)
      : shape == AuditionShape::lowPass  ? AC::makeLowPass  (sampleRate, freq, q)
                                         : AC::makeHighPass (sampleRate, freq, q);

    for (auto& f : auditionFilters)
        writeCoefficients (*f.state, vals);

    auditionApplied = { (int) shape, freq, q, true };
}

void EQProcessor::updateDynamicsForSubBlock (const juce::AudioBuffer<float>& input,
                                             int startSample, int numSamples)
{
    const double sr = sampleRateForAudio.load (std::memory_order_relaxed);

    if (sr <= 0.0 || numSamples <= 0)
        return;

    // Mono-Summe des Teilblocks auf dem STACK — kDynSubBlock ist die Obergrenze.
    float mono[kDynSubBlock];
    const int numCh = juce::jmax (1, input.getNumChannels());

    for (int s = 0; s < numSamples; ++s)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            sum += input.getSample (ch, startSample + s);
        mono[s] = sum / (float) numCh;
    }

    for (int i = 0; i < NumBands; ++i)
    {
        if (! dynEffective[i])
            continue;

        auto& d = dyn[i];

        // Spitzenwert dessen, was DIESES Band hoert.
        float peak = 0.0f;
        for (int s = 0; s < numSamples; ++s)
            peak = juce::jmax (peak, std::abs (d.detector.processSample (mono[s])));

        const float coeff = (peak > d.envelope) ? dynAttackCoeff : dynReleaseCoeff;
        d.envelope += (peak - d.envelope) * coeff;

        const float levelDB = juce::Decibels::gainToDecibels (d.envelope, -100.0f);
        const float over    = levelDB - bands[i].dynThresholdDB;
        const float ratio   = juce::jmax (1.0f, bands[i].dynRatio);
        const float cutDB   = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;

        // Nur nach UNTEN: das Band senkt ab, wenn seine Frequenz zu laut wird.
        const float target = bands[i].gainDB - cutDB;

        // Fuer die Anzeige (F4): der Wert, um den das Band GERADE zieht.
        // Nur ein Store pro Teilblock, kein Lock — die Oberflaeche liest ihn
        // im Timer und darf einen Zwischenwert verpassen.
        dynReductionDb[(size_t) i].store (-cutDB, std::memory_order_relaxed);

        // 0,05 dB Schwelle: darunter ist die Aenderung unhoerbar, und jedes
        // Nachschreiben kostet einen Koeffizientensatz.
        if (std::abs (target - d.appliedGain) > 0.05f)
        {
            writeBandCoefficients (i, target, sr);
            d.appliedGain = target;
        }
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

    // ---- Band-Solo: Filter und Puffer hier anlegen, nie im Audio-Thread ----
    for (auto& f : auditionFilters)
        f.prepare (spec);

    auditionApplied.valid = false;
    auditionBuffer.setSize ((int) spec.numChannels,
                            juce::jmax (1, maximumExpectedSamplesPerBlock),
                            false, true, false);

    // Apply all current band coefficients
    for (int i = 0; i < NumBands; ++i)
        updateBandCoefficients(i);

    // Prepare spectrum FIFO buffer (no reallocation needed if already correct size)
    spectrumBuffer.setSize(1, SpectrumFifoSize, false, true, false);

    // ---- Dynamik: Detektoren aufsetzen (Message-Thread, hier darf alloziert werden) ----
    juce::dsp::ProcessSpec monoSpec;
    monoSpec.sampleRate       = sampleRate;
    monoSpec.maximumBlockSize = static_cast<juce::uint32> (maximumExpectedSamplesPerBlock);
    monoSpec.numChannels      = 1;

    for (int i = 0; i < NumBands; ++i)
    {
        auto& d = dyn[i];

        // EINMAL anlegen — danach werden nur noch die Zahlen darin ueberschrieben.
        // Im Audio-Thread duerfte dieses Objekt nicht entstehen (I-01).
        d.detectorCoeffs = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f,
                                                                    1.0f, 0.0f, 0.0f);
        d.detector.coefficients = d.detectorCoeffs;
        d.detector.prepare (monoSpec);
        d.detector.reset();
        d.envelope    = 0.0f;
        d.appliedGain = bands[i].gainDB;
        d.detFreq     = -1.0f;     // erzwingt Neuberechnung des Bandpasses
        d.detQ        = -1.0f;
    }

    dynCoeffSourceAttack  = -1.0f;
    dynCoeffSourceRelease = -1.0f;
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

    const int numCh = buffer.getNumChannels();
    const int numSm = buffer.getNumSamples();

    // ---- Band-Solo: Eingang sichern, BEVOR die Kette ihn ueberschreibt -------
    // Der Puffer ist in prepareToPlay angelegt; passt der Block nicht hinein
    // (darf laut JUCE nicht vorkommen), laeuft der Block normal weiter statt
    // hier zu allozieren — I-01 gilt ohne Ausnahme.
    const int solo = soloedBand.load (std::memory_order_relaxed);
    const bool auditioning = solo >= 0 && solo < NumBands
                             && auditionBuffer.getNumSamples()  >= numSm
                             && auditionBuffer.getNumChannels() >= numCh;

    if (auditioning)
        for (int ch = 0; ch < numCh; ++ch)
            auditionBuffer.copyFrom (ch, 0, buffer, ch, 0, numSm);

    juce::dsp::AudioBlock<float> block(buffer);

    if (! anyDynamicActive)
    {
        // Kein Band regelt — der Block laeuft in EINEM Stueck durch, exakt wie vor
        // der Dynamik-Erweiterung. Die Teilblock-Schleife kostet also nichts,
        // solange niemand sie braucht.
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        eqChain.process(ctx);
    }
    else
    {
        // Teilblockweise: erst hoeren (am EINGANG, vor dem Filter), dann filtern.
        for (int pos = 0; pos < buffer.getNumSamples(); pos += kDynSubBlock)
        {
            const int len = juce::jmin (kDynSubBlock, buffer.getNumSamples() - pos);
            updateDynamicsForSubBlock (buffer, pos, len);

            auto sub = block.getSubBlock ((size_t) pos, (size_t) len);
            juce::dsp::ProcessContextReplacing<float> ctx (sub);
            eqChain.process (ctx);
        }
    }

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

    // ---- Band-Solo: Ausgang durch das Hoerfenster ersetzen ------------------
    // Die EQ-Kette oben ist absichtlich gelaufen (Filterzustaende und
    // Dynamik-Huellkurven bleiben warm) — ihr Ergebnis wird jetzt verworfen.
    // Gehoert wird der unbearbeitete Eingang durch zwei kaskadierte Sektionen.
    if (auditioning)
    {
        refreshAuditionCoefficients (solo, sampleRateForAudio.load (std::memory_order_relaxed));

        juce::dsp::AudioBlock<float> auditionBlock (auditionBuffer.getArrayOfWritePointers(),
                                                    (size_t) numCh, 0, (size_t) numSm);
        juce::dsp::ProcessContextReplacing<float> auditionCtx (auditionBlock);

        for (auto& f : auditionFilters)
            f.process (auditionCtx);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.copyFrom (ch, 0, auditionBuffer, ch, 0, numSm);
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

        if (p.dynOn != nullptr)
        {
            *p.dynOn     = band.dynOn;
            *p.dynThresh = band.dynThresholdDB;
            *p.dynRatio  = band.dynRatio;
        }
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
        bandXml->setAttribute ("dynOn",   b.dynOn ? 1 : 0);
        bandXml->setAttribute ("dynTh",   (double) b.dynThresholdDB);
        bandXml->setAttribute ("dynRa",   (double) b.dynRatio);
    }

    // Attack/Release sind geraeteweit — sie haengen an keinem Band.
    xml.setAttribute ("dynAttack",  dynAttackMs  != nullptr ? (double) dynAttackMs->get()  : 10.0);
    xml.setAttribute ("dynRelease", dynReleaseMs != nullptr ? (double) dynReleaseMs->get() : 120.0);

    copyXmlToBinary (xml, destData);
}

void EQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName ("EQState"))
        return;

    // Solo ist ein Abhoerzustand, kein Klangparameter: es wird nicht gespeichert
    // und beim Laden immer geloescht. Sonst koennte eine Session mit aktivem
    // Solo geoeffnet werden und klaenge duenn, ohne sichtbare Ursache.
    setSoloedBand (-1);

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
        // Aeltere Sessions kennen die Dynamik nicht — Vorgabe "aus" macht sie
        // wieder exakt zu dem statischen EQ, als der sie gespeichert wurden.
        b.dynOn          =           bandXml->getIntAttribute    ("dynOn",   0) != 0;
        b.dynThresholdDB = (float)   bandXml->getDoubleAttribute ("dynTh",  -18.0);
        b.dynRatio       = (float)   bandXml->getDoubleAttribute ("dynRa",    2.0);
        setBand (i, b);
    }

    if (dynAttackMs  != nullptr) *dynAttackMs  = (float) xml->getDoubleAttribute ("dynAttack",   10.0);
    if (dynReleaseMs != nullptr) *dynReleaseMs = (float) xml->getDoubleAttribute ("dynRelease", 120.0);
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
