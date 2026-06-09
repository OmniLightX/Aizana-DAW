#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/**
 * AddTracksDialog — Studio One-style "Add Tracks" dialog: create several mono or
 * stereo audio (or MIDI) tracks at once, instead of one at a time.
 *
 * Pure UI — collects (count, isMidi, isStereo, name) and hands them to onAdd; the
 * caller (ArrangementView) does the actual track creation. Shown via show().
 */
class AddTracksDialog : public juce::Component
{
public:
    std::function<void (int /*count*/, bool /*midi*/, bool /*stereo*/, juce::String /*name*/)> onAdd;

    AddTracksDialog()
    {
        title.setText ("Add Tracks", juce::dontSendNotification);
        title.setFont  (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        key (countKey,  "Count");
        key (typeKey,   "Type");
        key (formatKey, "Format");
        key (nameKey,   "Name");

        for (int i = 1; i <= 16; ++i) countCombo.addItem (juce::String (i), i);
        countCombo.setSelectedId (1, juce::dontSendNotification);
        countCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
        countCombo.setColour (juce::ComboBox::textColourId,       juce::Colours::white);
        addAndMakeVisible (countCombo);

        seg (typeAudio,    "Audio");  typeAudio.onClick    = [this] { setType (false); };
        seg (typeMidi,     "MIDI");   typeMidi.onClick     = [this] { setType (true);  };
        seg (formatMono,   "Mono");   formatMono.onClick   = [this] { setStereo (false); };
        seg (formatStereo, "Stereo"); formatStereo.onClick = [this] { setStereo (true);  };

        nameEditor.setText ("Audio", juce::dontSendNotification);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a2a));
        nameEditor.setColour (juce::TextEditor::textColourId,       juce::Colours::white);
        nameEditor.setColour (juce::TextEditor::outlineColourId,    juce::Colour (0xff444444));
        addAndMakeVisible (nameEditor);

        addButton.onClick = [this]
        {
            if (onAdd)
                onAdd (countCombo.getSelectedId(), midi, stereo, nameEditor.getText().trim());
            close();
        };
        addButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d6a2d));
        addAndMakeVisible (addButton);

        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        setType (false);
        setStereo (false);
        setSize (320, 248);
    }

    static void show (std::function<void (int, bool, bool, juce::String)> onAdd)
    {
        auto* dialog = new AddTracksDialog();
        dialog->onAdd = std::move (onAdd);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (dialog);
        opts.dialogTitle                  = "Add Tracks";
        opts.dialogBackgroundColour       = juce::Colour (0xff1e1e1e);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar            = true;
        opts.resizable                    = false;
        opts.launchAsync();
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e1e1e)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (24));
        area.removeFromTop (10);

        auto row = [&] (juce::Label& k) { auto r = area.removeFromTop (28); area.removeFromTop (6);
                                          k.setBounds (r.removeFromLeft (74)); return r; };

        countCombo.setBounds (row (countKey).removeFromLeft (76));

        { auto r = row (typeKey);   typeAudio.setBounds  (r.removeFromLeft (66).reduced (1));
                                    r.removeFromLeft (4); typeMidi.setBounds (r.removeFromLeft (66).reduced (1)); }
        { auto r = row (formatKey); formatMono.setBounds (r.removeFromLeft (66).reduced (1));
                                    r.removeFromLeft (4); formatStereo.setBounds (r.removeFromLeft (66).reduced (1)); }
        nameEditor.setBounds (row (nameKey).reduced (0, 2));

        auto buttons = area.removeFromBottom (30);
        addButton.setBounds    (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (90));
    }

private:
    juce::Label title, countKey, typeKey, formatKey, nameKey;
    juce::ComboBox   countCombo;
    juce::TextButton typeAudio, typeMidi, formatMono, formatStereo;
    juce::TextEditor nameEditor;
    juce::TextButton addButton { "Add" }, cancelButton { "Cancel" };
    bool midi = false, stereo = false;

    void setType (bool isMidi)
    {
        midi = isMidi;
        segOn (typeAudio, ! midi);
        segOn (typeMidi,    midi);
        // Format is audio-only; grey it out for MIDI.
        formatMono.setEnabled (! midi);
        formatStereo.setEnabled (! midi);
        // Default the name to the type if the user hasn't customised it.
        if (nameEditor.getText() == "Audio" || nameEditor.getText() == "MIDI")
            nameEditor.setText (midi ? "MIDI" : "Audio", juce::dontSendNotification);
    }

    void setStereo (bool s) { stereo = s; segOn (formatMono, ! stereo); segOn (formatStereo, stereo); }

    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    void key (juce::Label& l, const char* text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffbbbbbb));
        addAndMakeVisible (l);
    }

    void seg (juce::TextButton& b, const char* text)
    {
        b.setButtonText (text);
        b.setClickingTogglesState (false);   // invariant I-04: drive visuals ourselves
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff9aa0a6));
        b.setColour (juce::TextButton::textColourOnId,  juce::Colours::white);
        segOn (b, false);
        addAndMakeVisible (b);
    }

    static void segOn (juce::TextButton& b, bool on)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonColourId,
                     on ? juce::Colour (0xff4a90d9) : juce::Colour (0xff333333));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AddTracksDialog)
};
