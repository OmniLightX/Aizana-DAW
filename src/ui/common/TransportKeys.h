#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * TransportKeys — welche Transport-Aktion loest eine Taste aus?
 *
 * Warum das als eigener Kern existiert (User-Befund 2026-08-15): mit offenem
 * Plugin-Fenster tat die Leertaste NICHTS ausser einem Systemton. Das
 * Plugin-Fenster hat den Tastaturfokus, die Taste erreicht MainComponent nie,
 * und macOS quittiert eine unbeantwortete Taste mit NSBeep. Fuer den Anwender
 * sieht das aus, als sei die App haengengeblieben — dabei laeuft nur die
 * Wiedergabe weiter, waehrend er glaubt, gestoppt zu haben.
 *
 * Die Entscheidung "welche Aktion" ist reine Logik (inkl. Doppeltipp-Fenster)
 * und liegt deshalb hier: beide Aufrufer — das Hauptfenster und jedes
 * Plugin-Fenster — benutzen denselben Kern, statt die Regel zu kopieren und
 * auseinanderlaufen zu lassen. Und sie ist ohne GUI testbar.
 */
namespace TransportKeys
{
    enum class Action
    {
        none,
        playStop,        // Leertaste: laeuft → stopp, steht → los
        rewindToStart    // Leertaste zweimal schnell: stopp + zurueck auf 0
    };

    /** Doppeltipp-Fenster in Millisekunden (wie im Hauptfenster seit Phase 1). */
    inline constexpr juce::int64 kDoubleTapMs = 400;

    /**
     * Liefert die Aktion und schreibt den Zeitstempel fort.
     *
     * lastSpaceMs wird bei jeder erkannten Leertaste aktualisiert; der Aufrufer
     * haelt die Variable (pro Fenster eine — ein Doppeltipp ueber zwei
     * verschiedene Fenster hinweg waere keiner).
     */
    inline Action actionFor (const juce::KeyPress& key,
                             juce::int64 nowMs,
                             juce::int64& lastSpaceMs,
                             juce::int64 doubleTapMs = kDoubleTapMs)
    {
        if (key != juce::KeyPress::spaceKey)
            return Action::none;

        const bool isDoubleTap = (nowMs - lastSpaceMs) < doubleTapMs;
        lastSpaceMs = nowMs;
        return isDoubleTap ? Action::rewindToStart : Action::playStop;
    }
}
