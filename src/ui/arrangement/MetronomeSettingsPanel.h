#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * MetronomeSettingsPanel — popover (hosted in a CallOutBox) that bundles the
 * recording/metronome settings opened from the transport's metronome ▾ button:
 * Vorzähler (count-in), Pre-Roll, Downbeat accent and click volume. Sketch 006,
 * variant A — the Studio One pattern (one metronome popover instead of loose
 * transport text buttons).
 *
 * Pure UI: all state is read/written through caller-supplied callbacks, so the
 * panel has no engine dependency and is lifetime-safe (TransportBar wires the
 * callbacks with a SafePointer guard before launching the CallOutBox).
 */
class MetronomeSettingsPanel : public juce::Component
{
public:
    std::function<int()>      getCountIn,  getPreRoll;   // 0 = off, 1/2 = bars
    std::function<void(int)>  setCountIn,  setPreRoll;
    std::function<bool()>     getAccent;   std::function<void(bool)>  setAccent;
    std::function<float()>    getLevel;    std::function<void(float)> setLevel;   // 0..1

    MetronomeSettingsPanel()
    {
        title.setText ("Metronom & Aufnahme", juce::dontSendNotification);
        title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        styleKeyLabel (countInLabel, "Vorz\xc3\xa4hler");
        styleKeyLabel (preRollLabel, "Pre-Roll");
        styleKeyLabel (accentLabel,  "Downbeat betonen");
        styleKeyLabel (levelLabel,   "Klick-Lautst\xc3\xa4rke");
        for (auto* l : { &countInLabel, &preRollLabel, &accentLabel, &levelLabel })
            addAndMakeVisible (l);

        // Vier Takte sind der Wert, mit dem in der Praxis eingezaehlt wird —
        // "Klack, Klack, Klack, Klack, los". Deshalb gehoert er in die Auswahl.
        const char* segText[4] = { "Aus", "1", "2", "4" };
        for (int i = 0; i < 3; ++i)
        {
            styleSeg (countInSeg[i], segText[i]);
            countInSeg[i].setTooltip (juce::String::fromUTF8 ("Vorz\xc3\xa4hler \xe2\x80\x93 Klick-Takte vor dem Aufnahmestart. Der Vorlauf wird NICHT mit aufgenommen."));
            countInSeg[i].onClick = [this, i] { if (setCountIn) setCountIn (i); refresh(); };
            addAndMakeVisible (countInSeg[i]);

            styleSeg (preRollSeg[i], segText[i]);
            preRollSeg[i].setTooltip (juce::String::fromUTF8 ("Pre-Roll \xe2\x80\x93 h\xc3\xb6rbarer Anlauf (Takte) vor dem Aufnahmestart"));
            preRollSeg[i].onClick = [this, i] { if (setPreRoll) setPreRoll (i); refresh(); };
            addAndMakeVisible (preRollSeg[i]);
        }

        styleSeg (accentToggle, "Ein");
        accentToggle.setTooltip (juce::String::fromUTF8 ("Downbeat (Takt 1) lauter + h\xc3\xb6her betonen"));
        accentToggle.onClick = [this] { if (setAccent) setAccent (! (getAccent && getAccent())); refresh(); };
        addAndMakeVisible (accentToggle);

        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        levelSlider.setRange (0.0, 1.0, 0.01);
        levelSlider.setColour (juce::Slider::trackColourId,      juce::Colour (0xff4a90d9));
        levelSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a2a2a));
        levelSlider.setColour (juce::Slider::thumbColourId,      juce::Colours::white);
        levelSlider.setTooltip (juce::String::fromUTF8 ("Klick-Lautst\xc3\xa4rke des Metronoms"));
        levelSlider.onValueChange = [this] { if (setLevel) setLevel ((float) levelSlider.getValue()); };
        addAndMakeVisible (levelSlider);

        setSize (288, 214);
    }

    /** Pull current values into the controls. Call after wiring the callbacks. */
    void refresh()
    {
        const int ci = getCountIn ? getCountIn() : 0;
        const int pr = getPreRoll ? getPreRoll() : 0;
        static const int countInValue[4] = { 0, 1, 2, 4 };
        for (int i = 0; i < 4; ++i)
            setSegOn (countInSeg[i], countInValue[i] == ci);

        for (int i = 0; i < 3; ++i)
            setSegOn (preRollSeg[i], i == pr);

        const bool acc = getAccent && getAccent();
        setSegOn (accentToggle, acc);
        accentToggle.setButtonText (acc ? "Ein" : "Aus");

        if (getLevel)
            levelSlider.setValue (getLevel(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff262626)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        title.setBounds (area.removeFromTop (20));
        area.removeFromTop (8);

        auto segRow = [&] (juce::Label& key, juce::TextButton* seg, int count)
        {
            auto row = area.removeFromTop (26);
            key.setBounds (row.removeFromLeft (150));
            const int w = 36;
            for (int i = 0; i < count; ++i)
                seg[i].setBounds (row.removeFromLeft (w).reduced (1, 2));
            area.removeFromTop (8);
        };
        segRow (countInLabel, countInSeg, 4);
        segRow (preRollLabel, preRollSeg, 3);

        auto accentRow = area.removeFromTop (26);
        accentLabel.setBounds (accentRow.removeFromLeft (150));
        accentToggle.setBounds (accentRow.removeFromLeft (52).reduced (1, 2));
        area.removeFromTop (8);

        auto levelRow = area.removeFromTop (26);
        levelLabel.setBounds (levelRow.removeFromLeft (120));
        levelSlider.setBounds (levelRow.reduced (2, 4));
    }

private:
    juce::Label title, countInLabel, preRollLabel, accentLabel, levelLabel;
    juce::TextButton countInSeg[4], preRollSeg[3];
    juce::TextButton accentToggle;
    juce::Slider levelSlider;

    static void styleKeyLabel (juce::Label& l, const char* utf8)
    {
        l.setText (juce::String::fromUTF8 (utf8), juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffbbbbbb));
    }

    static void styleSeg (juce::TextButton& b, const char* text)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (false);   // invariant I-04: drive visuals ourselves
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9aa0a6));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
        setSegOn (b, false);
    }

    static void setSegOn (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xff4a90d9) : juce::Colour (0xff333333));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeSettingsPanel)
};
