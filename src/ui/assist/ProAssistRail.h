#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "app/ApplicationState.h"
#include "ai/IntentEngine.h"
#include "ai/HeuristicFeedbackEngine.h"
#include <vector>

namespace te = tracktion::engine;

/**
 * ProAssistRail — the opt-in dockable assist surface (Sketch 004, A of B+A).
 *
 * Shown on the right in PRO mode when assist is toggled on (the TransportBar's existing
 * "Assist" button drives uiState.assistEnabled; MainComponent shows/hides this rail to
 * match). Quiet and persistent — the standing companion to the ⌘K palette: pick an intent
 * category → an intent → real editable mechanic is applied to the selected track as ONE
 * undo step (IntentEngine → AICommandBridge), a "zuletzt gemacht" line records it, and the
 * master-meter observations from HeuristicFeedbackEngine stream below (each optionally a
 * one-tap intent). Same engines as the Beginner Coach rail, just calmer.
 *
 * Pure UI: holds no engine state of its own; targetProvider supplies the target track and
 * onClose toggles assist back off.
 */
class ProAssistRail : public juce::Component,
                      private juce::Timer
{
public:
    std::function<te::AudioTrack*()> targetProvider;   // current target (null → first track)
    std::function<void()>            onClose;          // × → toggle assist off

    explicit ProAssistRail (ApplicationState& appStateRef) : appState (appStateRef)
    {
        closeButton.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));   // ×
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff888888));
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        const IntentEngine::Category cats[] {
            IntentEngine::Category::LevelBalance, IntentEngine::Category::Character,
            IntentEngine::Category::SpaceDepth,   IntentEngine::Category::ReferenceLoudness };
        for (auto cat : cats)
        {
            auto* b = new juce::TextButton (IntentEngine::categoryLabel (cat));
            b->setClickingTogglesState (false);
            styleCat (*b);
            const int idx = catButtons.size();
            b->onClick = [this, idx] { selectedCat = idx; rebuildIntentButtons(); resized(); repaint(); };
            catButtons.add (b);
            addAndMakeVisible (b);
        }

        undoButton.setButtonText (juce::String::fromUTF8 ("R\xC3\xBC" "ckg\xC3\xA4ngig"));
        styleGhost (undoButton);
        undoButton.onClick = [this]
        {
            appState.editManager.undoLastAction();
            hasReveal = false;
            resized(); repaint();
        };
        addChildComponent (undoButton);

        rebuildIntentButtons();
    }

    void visibilityChanged() override
    {
        if (isVisible())
        {
            lastObsSig = {};
            refreshObservations();
            rebuildIntentButtons();
            resized();
            repaint();
            startTimer (1000);   // poll observations while visible (engine = logic, UI = timing)
        }
        else
        {
            stopTimer();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff252525));
        g.setColour (juce::Colour (0xff3a3a3a));
        g.fillRect (0, 0, 1, getHeight());   // left hairline (docked-panel feel)

        // header
        g.setColour (juce::Colour (0xff1fa882));
        g.fillEllipse ((float) headerAv.getX(), (float) headerAv.getY(), 22.0f, 22.0f);
        g.setColour (juce::Colour (0xff06241c));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("C", headerAv, juce::Justification::centred);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText ("Cloudes Assist", headerText, juce::Justification::centredLeft);

        // scope
        g.setColour (juce::Colour (0xff888888));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        juce::String scope = "Ziel: ";
        scope << (targetProvider && targetProvider() != nullptr
                    ? targetProvider()->getName() : juce::String ("Erster Track"));
        g.drawText (scope, scopeRow, juce::Justification::centredLeft, true);

        drawSection (g, sectIntentsY, juce::String::fromUTF8 ("WAS M\xC3\x96" "CHTEST DU TUN?"));

        // reveal (zuletzt gemacht)
        if (hasReveal)
        {
            g.setColour (juce::Colour (0xff1fa882));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (juce::String::fromUTF8 ("ZULETZT: ") + revealTitle.toUpperCase(),
                        revealTitleRow, juce::Justification::centredLeft, true);
            g.setColour (juce::Colour (0xffb6bcc2));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawFittedText (revealBody, revealBodyRow, juce::Justification::topLeft, 3);
        }

        if (! obs.empty())
            drawSection (g, sectObsY, juce::String::fromUTF8 ("BEOBACHTUNGEN \xC2\xB7 MASTER-METER"));

        for (size_t i = 0; i < obs.size() && i < obsRows.size(); ++i)
        {
            auto row = obsRows[i];
            g.setColour (juce::Colour (0xff111111));
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            g.setColour (obs[i].hasIntent ? juce::Colour (0xffff8800) : juce::Colour (0xff00cc44));
            g.fillEllipse ((float) row.getX() + 9, (float) row.getCentreY() - 3, 6.0f, 6.0f);
            g.setColour (juce::Colour (0xffcccccc));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            auto textArea = row.reduced (22, 5).withTrimmedRight (obs[i].hasIntent ? 70 : 6);
            g.drawFittedText (obs[i].message, textArea, juce::Justification::centredLeft, 2);
        }

        // footer ⌘K hint
        g.setColour (juce::Colour (0xff555555));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (juce::String::fromUTF8 ("Schneller: \xE2\x8C\x98K \xC3\xB6" "ffnet die Befehlspalette"),
                    juce::Rectangle<int> (0, getHeight() - 26, getWidth(), 20),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (13, 12);

        headerAv   = { area.getX(), area.getY(), 22, 22 };
        headerText = { area.getX() + 30, area.getY(), area.getWidth() - 60, 22 };
        closeButton.setBounds (area.getRight() - 24, area.getY(), 24, 24);

        int y = area.getY() + 34;
        scopeRow = { area.getX(), y, area.getWidth(), 18 }; y += 26;

        sectIntentsY = y; y += 20;

        // category buttons (2 columns)
        const int colW = (area.getWidth() - 6) / 2;
        for (int i = 0; i < catButtons.size(); ++i)
        {
            const int cx = area.getX() + (i % 2) * (colW + 6);
            const int cy = y + (i / 2) * 30;
            catButtons[i]->setBounds (cx, cy, colW, 26);
            catButtons[i]->setColour (juce::TextButton::buttonColourId,
                                      i == selectedCat ? juce::Colour (0xff4488ff) : juce::Colour (0xff333333));
            catButtons[i]->setColour (juce::TextButton::textColourOffId,
                                      i == selectedCat ? juce::Colours::white : juce::Colour (0xffcccccc));
        }
        y += ((catButtons.size() + 1) / 2) * 30 + 6;

        // intent buttons (stacked, full width)
        for (auto* b : intentButtons)
        {
            b->setBounds (area.getX(), y, area.getWidth(), 28);
            y += 32;
        }
        y += 4;

        // reveal
        if (hasReveal)
        {
            revealTitleRow = { area.getX(), y, area.getWidth(), 16 }; y += 18;
            revealBodyRow  = { area.getX(), y, area.getWidth(), 46 }; y += 48;
            undoButton.setVisible (true);
            undoButton.setBounds (area.getX(), y, 110, 24); y += 32;
        }
        else
        {
            undoButton.setVisible (false);
        }

        // observations
        obsRows.clear();
        if (! obs.empty())
        {
            sectObsY = y; y += 20;
            for (size_t i = 0; i < obs.size(); ++i)
            {
                juce::Rectangle<int> row (area.getX(), y, area.getWidth(), 40);
                obsRows.push_back (row);
                if (i < (size_t) obsFix.size())
                {
                    if (obs[i].hasIntent)
                    {
                        obsFix[(int) i]->setVisible (true);
                        obsFix[(int) i]->setBounds (row.getRight() - 64, row.getCentreY() - 11, 60, 22);
                    }
                    else
                    {
                        obsFix[(int) i]->setVisible (false);
                    }
                }
                y += 46;
            }
        }
    }

private:
    void timerCallback() override { refreshObservations(); }

    void refreshObservations()
    {
        if (! isVisible())
            return;

        auto fresh = appState.heuristicFeedbackEngine.current();
        if (fresh.size() > 3)
            fresh.resize (3);

        juce::String sig;
        for (auto& f : fresh)
            sig << f.message << (f.hasIntent ? "*" : "") << '|';
        if (sig == lastObsSig)
            return;
        lastObsSig = sig;
        obs = std::move (fresh);

        // (Re)build the per-observation one-tap "fix" buttons.
        obsFix.clear();
        for (size_t i = 0; i < obs.size(); ++i)
        {
            auto* b = new juce::TextButton (obs[i].hasIntent ? IntentEngine::label (obs[i].suggestedIntent)
                                                             : juce::String());
            styleGhost (*b);
            if (obs[i].hasIntent)
            {
                const auto intent = obs[i].suggestedIntent;
                b->onClick = [this, intent] { runIntent (intent); };
            }
            obsFix.add (b);
            addChildComponent (b);
        }

        resized();
        repaint();
    }

    void rebuildIntentButtons()
    {
        intentButtons.clear();
        const auto cats = std::vector<IntentEngine::Category> {
            IntentEngine::Category::LevelBalance, IntentEngine::Category::Character,
            IntentEngine::Category::SpaceDepth,   IntentEngine::Category::ReferenceLoudness };
        const auto cat = cats[(size_t) juce::jlimit (0, 3, selectedCat)];

        for (auto intent : IntentEngine::intentsFor (cat))
        {
            auto* b = new juce::TextButton (IntentEngine::label (intent));
            styleIntent (*b);
            b->onClick = [this, intent] { runIntent (intent); };
            intentButtons.add (b);
            addAndMakeVisible (b);
        }
    }

    void runIntent (IntentEngine::Intent intent)
    {
        // Mirror BeginnerView::runIntent / MainComponent::applyAssistIntent: real, editable
        // mechanic applied as ONE undo step. The rail also records it as the "zuletzt" reveal.
        auto* target = targetProvider ? targetProvider() : nullptr;
        const auto plan = appState.intentEngine.plan (intent, target);
        if (! plan.valid)
            return;

        appState.aiCommandBridge.enqueueBatch (plan.description, plan.commands);
        revealTitle = plan.description;
        revealBody  = plan.mechanic;
        hasReveal   = true;
        resized();
        repaint();
    }

    static void styleCat (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcccccc));
    }
    static void styleIntent (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2a2a));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff4488ff));
    }
    static void styleGhost (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff333333));
        b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcccccc));
    }

    void drawSection (juce::Graphics& g, int y, const juce::String& label)
    {
        g.setColour (juce::Colour (0xff666666));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (label, juce::Rectangle<int> (13, y, getWidth() - 26, 16),
                    juce::Justification::centredLeft, true);
    }

    ApplicationState& appState;

    juce::TextButton                   closeButton;
    juce::OwnedArray<juce::TextButton> catButtons;
    juce::OwnedArray<juce::TextButton> intentButtons;
    int                                selectedCat = 0;

    bool             hasReveal = false;
    juce::String     revealTitle, revealBody;
    juce::TextButton undoButton;

    std::vector<HeuristicFeedbackEngine::Feedback> obs;
    juce::OwnedArray<juce::TextButton>             obsFix;
    juce::String                                   lastObsSig;

    // painted-region rects computed in resized()
    juce::Rectangle<int> headerAv, headerText, scopeRow, revealTitleRow, revealBodyRow;
    int sectIntentsY = 0, sectObsY = 0;
    std::vector<juce::Rectangle<int>> obsRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProAssistRail)
};
