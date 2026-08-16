#include "LiveMidiPreview.h"
#include "engine/InstrumentTracks.h"

namespace aizana
{
    namespace
    {
        te::AudioTrack* asAudioTrack (te::Track* t)
        {
            return dynamic_cast<te::AudioTrack*> (t);
        }

        /** Eine feste Quell-Kennung fuer alles, was aus der Oberflaeche kommt.
            TE unterscheidet damit Quellen (fuer MPE); eine pro Prozess genuegt. */
        te::MPESourceID uiSourceID()
        {
            static const te::MPESourceID id = te::createUniqueMPESourceID();
            return id;
        }
    }

    // ── BEIDE Wege gleichzeitig, und das ist Absicht ─────────────────────────
    //
    // GEHOERT wird ueber `injectLiveMidiMessage` — direkt in den Wiedergabe-Graphen
    // der Spur. Das ist der Weg, der nachweislich funktioniert, unabhaengig von
    // Geraeten, Monitoring und Transportzustand.
    //
    // AUFGENOMMEN wird ueber das virtuelle Keyboard-Geraet. Nur dort kommt die
    // Note am Aufnahmepfad vorbei (User-Befund: "die Melodie wird nicht
    // aufgenommen"); in Logic ist das Musical-Typing-Fenster ausdruecklich eine
    // vollwertige Aufnahmequelle.
    //
    // ⚠️ FEHLER, DEN ICH HIER SCHON GEMACHT HABE (2026-08-15): erst stand hier
    // ein ENTWEDER-ODER — liegt das Geraet auf der Spur, geht die Note NUR
    // dorthin. Ergebnis: gar kein Ton mehr, weil das Geraet ohne Monitoring
    // nichts hoerbar macht. Ein funktionierender Weg wurde gegen einen
    // bedingten getauscht. Deshalb jetzt beides, und das Monitoring des Geraets
    // bleibt AUS (siehe routeKeyboardToTrack) — sonst hoerte man doppelt.

    void sendLivePreviewNoteOn (te::Track* track, int pitch, int velocity)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        // 1. Hoeren (immer)
        at->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, pitch,
                                       (juce::uint8) juce::jlimit (1, 127, velocity)),
                                   uiSourceID());

        // 2. Aufnehmen (wenn es das Geraet gibt)
        sendKeyboardNoteOn (at->edit.engine, pitch, velocity);
    }

    void sendLivePreviewNoteOff (te::Track* track, int pitch)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        at->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, pitch), uiSourceID());
        sendKeyboardNoteOff (at->edit.engine, pitch);
    }

    void sendLivePreviewAllNotesOff (te::Track* track)
    {
        auto* at = asAudioTrack (track);
        if (at == nullptr)
            return;

        // Einzelne Note-Offs statt "all notes off" (CC 123): nicht jedes Instrument
        // wertet den Controller aus, eine haengende Note ist aber das Schlimmste,
        // was hier passieren kann.
        for (int p = 0; p < 128; ++p)
            at->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, p), uiSourceID());
    }
}
