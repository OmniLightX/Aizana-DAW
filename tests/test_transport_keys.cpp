// test_transport_keys.cpp — F3: Transport-Tasten auch mit offenem Plugin-Fenster.
//
// User-Befund 2026-08-15: "Wenn ich im EQ arbeite und auf die Leertaste klicke,
// bleibt das Projekt nicht stehen, sondern laeuft weiter und ich kriege so ein
// Geraeusch wie 'Taste nicht erkannt'."
//
// Ursache: das Plugin-Fenster hat den Tastaturfokus, die Taste erreicht
// MainComponent nie, und macOS quittiert eine unbeantwortete Taste mit NSBeep.
// Fuer den Anwender sieht das aus wie eine haengende App — dabei laeuft nur die
// Wiedergabe weiter, waehrend er glaubt, gestoppt zu haben.
//
// Die Entscheidung "welche Aktion" liegt jetzt in EINEM Kern, den Hauptfenster
// UND Plugin-Fenster benutzen. Diese Tests halten sein Verhalten fest — vor
// allem das Doppeltipp-Fenster, das sonst leicht auseinanderlaeuft.

#include <catch2/catch_test_macros.hpp>
#include "ui/common/TransportKeys.h"

TEST_CASE ("TransportKeys: Leertaste startet und stoppt, Doppeltipp spult zurueck", "[transport][f3]")
{
    juce::int64 last = 0;

    SECTION ("einzelner Druck = Start/Stopp")
    {
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 10'000, last)
               == TransportKeys::Action::playStop);
        CHECK (last == 10'000);
    }

    SECTION ("zweiter Druck innerhalb des Fensters = zurueck auf Null")
    {
        TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 10'000, last);
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 10'200, last)
               == TransportKeys::Action::rewindToStart);
    }

    SECTION ("knapp AUSSERHALB des Fensters ist kein Doppeltipp")
    {
        TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 10'000, last);
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 10'400, last)
               == TransportKeys::Action::playStop);
    }

    SECTION ("drei schnelle Druecke: der dritte ist wieder ein Doppeltipp zum zweiten")
    {
        TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 1'000, last);
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 1'100, last)
               == TransportKeys::Action::rewindToStart);
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::spaceKey), 1'200, last)
               == TransportKeys::Action::rewindToStart);
    }

    SECTION ("andere Tasten bleiben unberuehrt und verschieben den Zeitstempel nicht")
    {
        last = 5'000;
        CHECK (TransportKeys::actionFor (juce::KeyPress (juce::KeyPress::returnKey), 9'999, last)
               == TransportKeys::Action::none);
        CHECK (TransportKeys::actionFor (juce::KeyPress ('a'), 9'999, last)
               == TransportKeys::Action::none);
        CHECK (last == 5'000);   // unveraendert — sonst waere das naechste Leerzeichen falsch bewertet
    }
}
