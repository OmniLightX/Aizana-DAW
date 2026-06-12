#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ai/SessionPlanner.h"

/**
 * StemImportDialog — Phase 3, Iteration 1, Slice 4 (decision E3: Pro mode).
 *
 * Preview table for the AI stem setup: one row per file showing
 * file → instrument (editable) → group (combo) → initial gain. The user
 * reviews/corrects the detection, then applies — Beginner mode skips this
 * dialog entirely (instant apply + one undo).
 *
 * Pure UI: edits a SessionPlan copy and hands it to onApply. The caller
 * (MainComponent) owns the StemImportEngine that turns it into a session.
 */
class StemImportDialog : public juce::Component
{
public:
    std::function<void (const SessionPlan&)> onApply;

    explicit StemImportDialog (const SessionPlan& initialPlan)
        : plan (initialPlan)
    {
        title.setText (juce::String::fromUTF8 ("KI-Setup — erkannte Stems"),
                       juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        header (hdrFile,  "Datei");
        header (hdrInstr, "Instrument");
        header (hdrGroup, "Gruppe");
        header (hdrGain,  "Gain");

        for (int i = 0; i < plan.tracks.size(); ++i)
            rows.add (new Row (*this, i));

        rowViewport.setViewedComponent (&rowHolder, false);
        rowViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (rowViewport);
        for (auto* r : rows)
            rowHolder.addAndMakeVisible (r);

        applyButton.setButtonText ("Anwenden");
        applyButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d6a2d));
        applyButton.onClick = [this]
        {
            pullEdits();
            if (onApply)
                onApply (plan);
            close();
        };
        addAndMakeVisible (applyButton);

        cancelButton.setButtonText ("Abbrechen");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        const int rowsVisible = juce::jmin (12, plan.tracks.size());
        setSize (620, 96 + rowsVisible * kRowH + 56);
    }

    /** Shows the dialog asynchronously (non-modal-loop, async pattern). */
    static void show (juce::Component& parent, const SessionPlan& plan,
                      std::function<void (const SessionPlan&)> onApply)
    {
        auto* content = new StemImportDialog (plan);
        content->onApply = std::move (onApply);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (content);
        opts.dialogTitle = "KI-Setup";
        opts.dialogBackgroundColour = juce::Colour (0xff1e1e1e);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = false;
        opts.componentToCentreAround = &parent;
        opts.launchAsync();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1e1e));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);

        auto hdr = area.removeFromTop (20);
        layoutColumns (hdr, hdrFile, hdrInstr, hdrGroup, hdrGain);
        area.removeFromTop (4);

        auto buttonsArea = area.removeFromBottom (36);
        rowViewport.setBounds (area.reduced (0, 2));

        rowHolder.setSize (rowViewport.getWidth() - 12, rows.size() * kRowH);
        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, i * kRowH, rowHolder.getWidth(), kRowH);

        cancelButton.setBounds (buttonsArea.removeFromRight (110).reduced (2));
        applyButton.setBounds  (buttonsArea.removeFromRight (110).reduced (2));
    }

private:
    static constexpr int kRowH = 30;

    /** Column layout shared by header and rows: file 38% · instrument 26% · group 22% · gain rest. */
    template <typename A, typename B, typename C, typename D>
    static void layoutColumns (juce::Rectangle<int> r, A& file, B& instr, C& group, D& gain)
    {
        const int w = r.getWidth();
        file.setBounds  (r.removeFromLeft (int (w * 0.38f)).reduced (2));
        instr.setBounds (r.removeFromLeft (int (w * 0.26f)).reduced (2));
        group.setBounds (r.removeFromLeft (int (w * 0.22f)).reduced (2));
        gain.setBounds  (r.reduced (2));
    }

    struct Row : juce::Component
    {
        Row (StemImportDialog& d, int trackIndex) : owner (d), index (trackIndex)
        {
            const auto& t = owner.plan.tracks.getReference (index);

            fileLabel.setText (t.file.getFileName(), juce::dontSendNotification);
            fileLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            fileLabel.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (fileLabel);

            instrEditor.setText (t.name, juce::dontSendNotification);
            instrEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a2a));
            instrEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            instrEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff444444));
            addAndMakeVisible (instrEditor);

            int id = 1;
            for (auto* g : { "Drums", "Bass", "Guitars", "Keys", "Vocals", "FX" })
                groupCombo.addItem (g, id++);
            groupCombo.addItem (juce::String::fromUTF8 ("— direkt an Master"), 99);
            groupCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2a2a2a));
            groupCombo.setColour (juce::ComboBox::textColourId, juce::Colours::white);
            selectGroup (t.group);
            addAndMakeVisible (groupCombo);

            gainLabel.setText (juce::String (t.gainDb, 1) + " dB", juce::dontSendNotification);
            gainLabel.setColour (juce::Label::textColourId,
                                 t.confidence < 0.5 ? juce::Colours::orange
                                                    : juce::Colours::lightgrey);
            // Low confidence rows show orange gain text as a "check me" hint.
            addAndMakeVisible (gainLabel);
        }

        void selectGroup (const juce::String& g)
        {
            const char* names[] = { "Drums", "Bass", "Guitars", "Keys", "Vocals", "FX" };
            for (int i = 0; i < 6; ++i)
                if (g == names[i]) { groupCombo.setSelectedId (i + 1, juce::dontSendNotification); return; }
            groupCombo.setSelectedId (99, juce::dontSendNotification);   // direct to master
        }

        juce::String selectedGroup() const
        {
            const int id = groupCombo.getSelectedId();
            const char* names[] = { "Drums", "Bass", "Guitars", "Keys", "Vocals", "FX" };
            return (id >= 1 && id <= 6) ? juce::String (names[id - 1]) : juce::String();
        }

        void resized() override
        {
            layoutColumns (getLocalBounds(), fileLabel, instrEditor, groupCombo, gainLabel);
        }

        StemImportDialog& owner;
        int index;
        juce::Label      fileLabel, gainLabel;
        juce::TextEditor instrEditor;
        juce::ComboBox   groupCombo;
    };

    /** Copy the user's edits back into the plan and rebuild the group list. */
    void pullEdits()
    {
        for (auto* row : rows)
        {
            auto& t = plan.tracks.getReference (row->index);
            const auto name = row->instrEditor.getText().trim();
            if (name.isNotEmpty())
                t.name = name;
            t.group  = row->selectedGroup();
            t.colour = SessionPlanner::colourForGroup (t.group.isEmpty() ? "Other" : t.group);
        }
        SessionPlanner::rebuildGroups (plan);
    }

    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    void header (juce::Label& l, const char* text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
        addAndMakeVisible (l);
    }

    SessionPlan plan;
    juce::Label title, hdrFile, hdrInstr, hdrGroup, hdrGain;
    juce::Viewport rowViewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
    juce::TextButton applyButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemImportDialog)
};
