#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * MusicalTypingWindow — Instrumente ohne angeschlossenes Keyboard spielen.
 *
 * Vorbild ist Logics „Musical Typing" (⌘K): ein kleines Fenster mit einer
 * Klaviatur, das die RECHNERTASTATUR als Klaviatur benutzt, solange es vorn ist.
 * Ohne so etwas kann man ein Instrument nur beurteilen, indem man Noten ZEICHNET
 * — fuer „ist das spielbar?" ist das keine Antwort.
 *
 * Belegung (wie bei Logic, JUCE bringt sie mit):
 *   A W S E D F T G Y H U J  =  C C# D D# E F F# G G# A A# B
 *   linke/rechte Pfeiltaste  =  Oktave tiefer/hoeher
 *
 * Gespielt wird immer auf der AUSGEWAEHLTEN Spur; die Noten gehen ueber
 * `AudioTrack::injectLiveMidiMessage` in deren Wiedergabe-Graph (siehe
 * LiveMidiPreview.h — der naheliegende Weg ueber die MIDI-Ausgangsgeraete
 * erreicht eingebaute Instrumente NICHT).
 */
class MusicalTypingWindow : public juce::DocumentWindow
{
public:
    /** trackProvider liefert die Spur, auf der gespielt werden soll (oder nullptr).
        Als Funktion, damit ein Spurwechsel im Arrangement sofort greift, ohne dass
        das Fenster neu aufgemacht werden muss. */
    explicit MusicalTypingWindow (std::function<te::Track*()> trackProvider);
    ~MusicalTypingWindow() override;

    void closeButtonPressed() override;

    /** Wird beim Schliessen gerufen — der Besitzer raeumt den unique_ptr weg. */
    std::function<void()> onClose;

private:
    class Content;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicalTypingWindow)
};
