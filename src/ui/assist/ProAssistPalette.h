#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ai/IntentEngine.h"
#include <vector>

/**
 * ProAssistPalette — the ⌘K command palette (Sketch 004, B+A · the "primary" surface).
 *
 * In PRO mode the AI assist is quiet by default; ⌘K summons this centered, keyboard-first
 * palette over a dimmed window. The user types/arrows through the 12 IntentEngine intents
 * (grouped by the 4 categories, label-filtered), presses ↵ to apply to the currently
 * targeted track — the owner wires onApply → IntentEngine::plan + AICommandBridge (one undo
 * step) — and the palette dismisses. esc / click-outside also dismiss.
 *
 * Pure UI: it holds no engine state. The owner supplies onApply / onDismiss and an optional
 * targetName() for the scope chip. Full-window overlay so focus + click-outside are simple.
 */
class ProAssistPalette : public juce::Component,
                         private juce::KeyListener
{
public:
    std::function<void (IntentEngine::Intent)> onApply;
    std::function<void()>                      onDismiss;
    std::function<juce::String()>              targetName;   // scope chip (e.g. selected track)

    ProAssistPalette()
    {
        setWantsKeyboardFocus (true);

        search.setMultiLine (false);
        search.setReturnKeyStartsNewLine (false);
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        search.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff4488ff));
        search.setFont (juce::Font (juce::FontOptions (17.0f)));
        search.setTextToShowWhenEmpty ("Absicht eingeben — z. B. \"vorne\", \"warmer\", \"lauter\"...",
                                       juce::Colour (0xff666666));
        search.onTextChange     = [this] { rebuildEntries(); repaint(); };
        search.onEscapeKey      = [this] { dismiss(); };
        search.onReturnKey      = [this] { applySelected(); };
        search.addKeyListener (this);
        addAndMakeVisible (search);
    }

    /** Reset to a clean state and take focus. Call right after making it visible. */
    void reset()
    {
        search.setText ({}, juce::dontSendNotification);
        selected = 0;
        rebuildEntries();
        search.grabKeyboardFocus();
        repaint();
    }

    void resized() override
    {
        layoutCard();
        search.setBounds (inputBounds);
    }

    void paint (juce::Graphics& g) override
    {
        // Dim backdrop
        g.fillAll (juce::Colour (0x99000000));

        // Card
        g.setColour (juce::Colour (0xff202225));
        g.fillRoundedRectangle (card.toFloat(), 13.0f);
        g.setColour (juce::Colour (0x55ffffff));
        g.drawRoundedRectangle (card.toFloat(), 13.0f, 1.0f);

        // Input row: leading ✦ glyph + scope chip + divider
        g.setColour (juce::Colour (0xff1fa882));
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("\xE2\x9C\xA6"),
                    card.getX() + 14, inputBounds.getY(), 22, inputBounds.getHeight(),
                    juce::Justification::centredLeft);

        if (targetName)
        {
            const auto name = targetName();
            if (name.isNotEmpty())
            {
                juce::Rectangle<int> chip (card.getRight() - 14 - 130, inputBounds.getY() + 9, 130, 22);
                g.setColour (juce::Colour (0xff2a2a2a));
                g.fillRoundedRectangle (chip.toFloat(), 11.0f);
                g.setColour (juce::Colour (0xff888888));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText (name, chip.reduced (10, 0), juce::Justification::centredRight, true);
            }
        }

        g.setColour (juce::Colour (0xff3a3a3a));
        g.fillRect (card.getX(), inputBounds.getBottom(), card.getWidth(), 1);

        // Entries (group headers + selectable intent rows)
        for (const auto& h : headerRows)
        {
            g.setColour (juce::Colour (0xff666666));
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (h.text.toUpperCase(), h.bounds.reduced (16, 0),
                        juce::Justification::centredLeft, true);
        }

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            const auto& e = entries[(size_t) i];
            const bool sel = (i == selected);
            if (sel)
            {
                g.setColour (juce::Colour (0xff4488ff));
                g.fillRoundedRectangle (e.bounds.reduced (6, 1).toFloat(), 6.0f);
            }
            g.setColour (sel ? juce::Colours::white : juce::Colour (0xffe8e8e8));
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawText (IntentEngine::label (e.intent), e.bounds.reduced (22, 0),
                        juce::Justification::centredLeft, true);

            g.setColour (sel ? juce::Colour (0xcceaf2ff) : juce::Colour (0xff888888));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawText (hint (e.intent), e.bounds.reduced (22, 0).withTrimmedRight (4),
                        juce::Justification::centredRight, true);
        }

        // Footer hint
        g.setColour (juce::Colour (0xff555555));
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (juce::String::fromUTF8 ("\xE2\x86\x91\xE2\x86\x93 w\xC3\xA4hlen     \xE2\x86\xB5 anwenden (1 Undo)     esc schlie\xC3\x9F" "en"),
                    card.getX(), card.getBottom() - 26, card.getWidth(), 22,
                    juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! card.contains (e.getPosition()))
        {
            dismiss();
            return;
        }
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].bounds.contains (e.getPosition()))
            {
                selected = i;
                applySelected();
                return;
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].bounds.contains (e.getPosition()) && i != selected)
            {
                selected = i;
                repaint();
                return;
            }
    }

private:
    struct Entry  { IntentEngine::Intent intent; juce::Rectangle<int> bounds; };
    struct Header { juce::String text; juce::Rectangle<int> bounds; };

    juce::TextEditor      search;
    std::vector<Entry>    entries;
    std::vector<Header>   headerRows;
    int                   selected = 0;

    juce::Rectangle<int>  card;
    juce::Rectangle<int>  inputBounds;

    static constexpr int kCardW   = 560;
    static constexpr int kInputH  = 52;
    static constexpr int kHeaderH = 24;
    static constexpr int kRowH    = 36;
    static constexpr int kFootH   = 30;

    // Keyboard nav while the search field has focus (KeyListener fires first).
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (key == juce::KeyPress::upKey)    { move (-1); return true; }
        if (key == juce::KeyPress::downKey)  { move (+1); return true; }
        return false;   // return/escape handled by the editor callbacks
    }

    void move (int delta)
    {
        if (entries.empty()) return;
        selected = juce::jlimit (0, (int) entries.size() - 1, selected + delta);
        repaint();
    }

    void applySelected()
    {
        if (selected >= 0 && selected < (int) entries.size() && onApply)
            onApply (entries[(size_t) selected].intent);
        dismiss();
    }

    void dismiss() { if (onDismiss) onDismiss(); }

    // Build the filtered, grouped entry list and lay everything out.
    void rebuildEntries()
    {
        entries.clear();
        headerRows.clear();

        const auto q = search.getText().trim().toLowerCase();

        using Cat = IntentEngine::Category;
        const Cat cats[] { Cat::LevelBalance, Cat::Character, Cat::SpaceDepth, Cat::ReferenceLoudness };

        for (auto cat : cats)
        {
            std::vector<IntentEngine::Intent> matched;
            for (auto intent : IntentEngine::intentsFor (cat))
                if (q.isEmpty()
                    || IntentEngine::label (intent).toLowerCase().contains (q)
                    || matchesKeyword (q, intent))
                    matched.push_back (intent);

            if (matched.empty())
                continue;

            headerRows.push_back ({ IntentEngine::categoryLabel (cat), {} });
            for (auto intent : matched)
                entries.push_back ({ intent, {} });
        }

        selected = juce::jlimit (0, juce::jmax (0, (int) entries.size() - 1), selected);
        layoutCard();
    }

    // Card height follows the filtered content; lay out headers + rows in order.
    void layoutCard()
    {
        const int listH = (int) headerRows.size() * kHeaderH + (int) entries.size() * kRowH;
        const int cardH = kInputH + 8 + listH + kFootH + 8;

        card = { (getWidth() - kCardW) / 2, 84, kCardW, juce::jmax (kInputH + kFootH + 20, cardH) };
        inputBounds = { card.getX(), card.getY(), card.getWidth(), kInputH };
        // leave room for the ✦ glyph
        inputBounds = inputBounds.withTrimmedLeft (40).withTrimmedRight (150);

        int y = card.getY() + kInputH + 8;

        // Walk categories in the same order as rebuildEntries so headers + rows interleave.
        using Cat = IntentEngine::Category;
        const Cat cats[] { Cat::LevelBalance, Cat::Character, Cat::SpaceDepth, Cat::ReferenceLoudness };
        const auto q = search.getText().trim().toLowerCase();

        size_t hi = 0, ei = 0;
        for (auto cat : cats)
        {
            std::vector<IntentEngine::Intent> matched;
            for (auto intent : IntentEngine::intentsFor (cat))
                if (q.isEmpty()
                    || IntentEngine::label (intent).toLowerCase().contains (q)
                    || matchesKeyword (q, intent))
                    matched.push_back (intent);

            if (matched.empty())
                continue;

            if (hi < headerRows.size())
                headerRows[hi++].bounds = { card.getX(), y, card.getWidth(), kHeaderH };
            y += kHeaderH;

            for (size_t k = 0; k < matched.size() && ei < entries.size(); ++k, ++ei)
            {
                entries[ei].bounds = { card.getX(), y, card.getWidth(), kRowH };
                y += kRowH;
            }
        }
    }

    // Mirror of BeginnerView's keyword-lite mapping so typed German resolves to intents
    // even when it doesn't substring-match the label (Phase 3 replaces with real NLP).
    static bool matchesKeyword (const juce::String& s, IntentEngine::Intent intent)
    {
        using I = IntentEngine::Intent;
        auto has = [&] (const char* k) { return s.contains (k); };
        switch (intent)
        {
            case I::VocalForward:      return has ("vorne") || has ("gesang") || has ("vocal");
            case I::QuieterInMix:      return has ("leis");
            case I::CenterInMix:       return has ("mittig") || has ("zentr");
            case I::Warmer:            return has ("warm");
            case I::Clearer:           return has ("klar") || has ("praesen") || has ("präsen");
            case I::MorePunch:         return has ("punch") || has ("druck");
            case I::MoreReverb:        return has ("hall") || has ("raum");
            case I::Wider:             return has ("breit");
            case I::Closer:            return has ("naeh") || has ("näh") || has ("nah");
            case I::Louder:            return has ("laut");
            case I::GlueMix:           return has ("verkleb") || has ("glue") || has ("kleb");
            case I::MatchPopReference: return has ("pop") || has ("referenz");
        }
        return false;
    }

    // One-line "what it does" hint shown right-aligned per row (boilerplate-speed cue).
    static juce::String hint (IntentEngine::Intent intent)
    {
        using I = IntentEngine::Intent;
        switch (intent)
        {
            case I::VocalForward:      return "+Pegel \xC2\xB7 EQ-Pr\xC3\xA4senz \xC2\xB7 Comp";
            case I::QuieterInMix:      return "\xE2\x88\x92Pegel";
            case I::CenterInMix:       return "Pan \xE2\x86\x92 Mitte";
            case I::Warmer:            return "EQ Low-Shelf +, High \xE2\x88\x92";
            case I::Clearer:           return "EQ Pr\xC3\xA4senz/Air";
            case I::MorePunch:         return "Comp schneller Attack";
            case I::MoreReverb:        return "Reverb-Send";
            case I::Wider:             return "Stereo-Breite";
            case I::Closer:            return "weniger Hall \xC2\xB7 +Pegel";
            case I::Louder:            return "Master \xC2\xB7 Limiter";
            case I::GlueMix:           return "Master \xC2\xB7 Bus-Comp";
            case I::MatchPopReference: return "Loudness \xE2\x86\x92 Pop";
        }
        return {};
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProAssistPalette)
};
