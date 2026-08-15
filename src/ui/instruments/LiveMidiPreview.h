#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace aizana
{
    /**
     * Live-Noten in eine Spur einspeisen — der Weg, auf dem ein eingebautes
     * Instrument sie ueberhaupt hoert.
     *
     * Warum das eine eigene Stelle ist: der naheliegende Weg ueber die MIDI-
     * AUSGANGSGERAETE der Engine erreicht nur externe Klangerzeuger. Wer keins
     * angeschlossen hat — der Normalfall, und bei Aizana Drums/Synth immer —
     * hoert dann gar nichts, ohne dass irgendwo ein Fehler erscheint. TE bietet
     * dafuer `AudioTrack::injectLiveMidiMessage`; damit landet die Note im
     * Wiedergabe-Graph der Spur, also im Instrument.
     *
     * Beide Funktionen laufen auf dem Message-Thread (Mausklick, Tastendruck).
     */
    void sendLivePreviewNoteOn  (te::Track* track, int pitch, int velocity);
    void sendLivePreviewNoteOff (te::Track* track, int pitch);

    /** Alle 128 Noten auf der Spur beenden — beim Schliessen/Fokusverlust, damit
        keine Note haengen bleibt. */
    void sendLivePreviewAllNotesOff (te::Track* track);
}
