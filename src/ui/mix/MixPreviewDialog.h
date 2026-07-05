#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ai/MixingEngine.h"

/**
 * MixPreviewDialog — Phase 4 / Iteration 1, Slice 4 (Pro mode).
 *
 * Review table for the heuristic mix: one row per MixAction showing a toggle,
 * the move (describe()) and the jargon-free reason. The user un-checks the moves
 * they don't want, then applies. Beginner mode skips this entirely (instant apply
 * + one undo).
 *
 * Pure UI: builds a filtered MixPlan copy from the checked rows and hands it to
 * onApply. The caller (MainComponent) owns the MixApplyEngine that applies it.
 */
class MixPreviewDialog : public juce::Component
{
public:
    std::function<void (const aizana::MixPlan&)> onApply;

    explicit MixPreviewDialog (const aizana::MixPlan& initialPlan)
        : plan (initialPlan)
    {
        title.setText (juce::String::fromUTF8 ("KI-Mix — vorgeschlagene Schritte"),
                       juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        subtitle.setText (plan.summary, juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (12.0f));
        subtitle.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
        addAndMakeVisible (subtitle);

        for (int i = 0; i < (int) plan.actions.size(); ++i)
            rows.add (new Row (plan.actions[(size_t) i]));

        rowViewport.setViewedComponent (&rowHolder, false);
        rowViewport.setScrollBarsShown (true, false);
        addAndMakeVisible (rowViewport);
        for (auto* r : rows)
            rowHolder.addAndMakeVisible (r);

        applyButton.setButtonText ("Mix anwenden");
        applyButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2d6a2d));
        applyButton.onClick = [this]
        {
            aizana::MixPlan filtered;
            filtered.summary = plan.summary;
            for (int i = 0; i < rows.size(); ++i)
                if (rows[i]->enabled.getToggleState())
                    filtered.add (plan.actions[(size_t) i]);

            if (onApply && ! filtered.isEmpty())
                onApply (filtered);
            close();
        };
        addAndMakeVisible (applyButton);

        cancelButton.setButtonText ("Abbrechen");
        cancelButton.onClick = [this] { close(); };
        addAndMakeVisible (cancelButton);

        const int rowsVisible = juce::jlimit (4, 14, (int) plan.actions.size());
        setSize (640, 104 + rowsVisible * kRowH + 48);
    }

    /** Shows the dialog asynchronously (no modal loop). */
    static void show (juce::Component& parent, const aizana::MixPlan& plan,
                      std::function<void (const aizana::MixPlan&)> onApply)
    {
        auto* content = new MixPreviewDialog (plan);
        content->onApply = std::move (onApply);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (content);
        opts.dialogTitle = "KI-Mix";
        opts.dialogBackgroundColour = juce::Colour (0xff1e1e1e);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar = true;
        opts.resizable = false;
        opts.componentToCentreAround = &parent;
        opts.launchAsync();
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e1e1e)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        title.setBounds (area.removeFromTop (24));
        subtitle.setBounds (area.removeFromTop (18));
        area.removeFromTop (6);

        auto buttonsArea = area.removeFromBottom (36);
        rowViewport.setBounds (area.reduced (0, 2));

        rowHolder.setSize (rowViewport.getWidth() - 12, rows.size() * kRowH);
        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, i * kRowH, rowHolder.getWidth(), kRowH);

        cancelButton.setBounds (buttonsArea.removeFromRight (120).reduced (2));
        applyButton.setBounds  (buttonsArea.removeFromRight (130).reduced (2));
    }

private:
    static constexpr int kRowH = 40;

    struct Row : juce::Component
    {
        explicit Row (const aizana::MixAction& a)
        {
            enabled.setToggleState (true, juce::dontSendNotification);
            addAndMakeVisible (enabled);

            move.setText (a.describe(), juce::dontSendNotification);
            move.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            move.setColour (juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible (move);

            reason.setText (a.reason, juce::dontSendNotification);
            reason.setFont (juce::FontOptions (11.5f));
            reason.setColour (juce::Label::textColourId, juce::Colour (0xff9aa0a6));
            reason.setMinimumHorizontalScale (0.85f);
            addAndMakeVisible (reason);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            enabled.setBounds (r.removeFromLeft (32));
            move.setBounds   (r.removeFromTop (r.getHeight() / 2));
            reason.setBounds (r);
        }

        juce::ToggleButton enabled;
        juce::Label        move, reason;
    };

    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    aizana::MixPlan plan;
    juce::Label title, subtitle;
    juce::Viewport rowViewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
    juce::TextButton applyButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixPreviewDialog)
};
