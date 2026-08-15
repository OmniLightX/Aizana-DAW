#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <functional>

namespace te = tracktion::engine;

/**
 * InstrumentTracks — Instrumentenspuren als eigener Spurtyp, plus das
 * virtuelle Keyboard-Eingabegeraet, ueber das gespielte Noten AUFGENOMMEN
 * werden koennen.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Warum es das gibt (User-Befund 2026-08-15, beim Test des Synths)
 * ─────────────────────────────────────────────────────────────────────────────
 * Zwei Luecken, die zusammengehoeren:
 *
 * 1. "Wo finde ich denn den MIDI-Clip?" — Aizana kannte keine Instrumenten-
 *    spur. Man legte eine AUDIO-Spur an, haengte ein Instrument hinein, und
 *    hatte damit eine Spur, die die App selbst nicht als MIDI-Spur erkannte:
 *    `isMidiTrack()` riet am INHALT ("liegt hier ein MidiClip?") statt am Typ.
 *    Ohne Clip also: kein MIDI-Eingang waehlbar, Aufnahmeknopf verhielt sich
 *    wie bei Audio. Der zweite Weg ("Add MIDI Track" im Arrangement-Rechtsklick)
 *    legte zwar einen Clip an, aber kein Instrument — und war versteckt.
 *
 *    Die Referenz macht daraus EINEN Schritt: "To record MIDI data in Studio
 *    One, you will create an Instrument track" (Track > Add Instrument Track);
 *    Logic hat die Software-Instrument-Spur. Audio und MIDI liegen dort auf
 *    derselben Spurart, aber der Anwender waehlt EINMAL beim Anlegen.
 *
 * 2. "Wenn ich aufnehmen moechte, wird die Melodie nicht aufgenommen." Richtig:
 *    die Bildschirmklaviatur schickte ihre Noten ueber
 *    `AudioTrack::injectLiveMidiMessage` direkt in den WIEDERGABE-Graphen. Man
 *    hoerte sie, aber sie kamen nie am Aufnahmepfad vorbei. In Logic ist genau
 *    das anders: aufgenommen wird "with your MIDI keyboard, Musical Typing
 *    window, or onscreen keyboard" — die Tastatur ist eine vollwertige Quelle.
 *
 *    Deshalb hier ein echtes virtuelles Eingabegeraet (TEs
 *    `DeviceManager::createVirtualMidiDevice`). Die Klaviatur spielt hinein,
 *    und ab da laeuft alles ueber TEs normalen Weg: Monitoring, Aufnahme,
 *    Takes, Undo — nichts davon muss nachgebaut werden.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Threading: alles Message-Thread (Spur anlegen, Geraet zuweisen). Das Senden
 * einer Note kommt vom Mausklick bzw. Tastendruck, also ebenfalls von dort.
 */
namespace aizana
{
    /** Spurtyp-Merkmal im ValueTree. Bewusst eine EIGENE Eigenschaft und nicht
        der Kanaltyp der MixerEngine ("bus"/"fx"/"cue"): eine Instrumentenspur
        ist im Mixer ein ganz normaler Kanalzug. */
    inline constexpr const char* kTrackTypeProp = "aizanaTrackType";
    inline constexpr const char* kInstrumentTag = "instrument";

    /** Anzeigename des virtuellen Geraets — taucht so in der Eingangsliste auf. */
    inline constexpr const char* kKeyboardDeviceName = "Aizana Keyboard";

    /**
     * Ist das eine Instrumentenspur?
     *
     * Erst am Merkmal, dann als RUECKFALL am Inhalt: Sessions von vor dieser
     * Aenderung haben das Merkmal nicht, sollen sich aber weiter wie MIDI-Spuren
     * verhalten. (Migration ohne Anfassen der Datei — dieselbe Linie wie bei den
     * Legacy-Identifiern.)
     */
    bool isInstrumentTrack (const te::Track& track);

    /** Merkmal setzen (ohne UndoManager — reine Typ-Auszeichnung beim Anlegen). */
    void markAsInstrumentTrack (te::Track& track);

    /**
     * Das virtuelle Keyboard-Geraet holen und bei Bedarf anlegen.
     * nullptr, wenn die Engine noch keine Geraete kennt (headless/fruehe Phase).
     */
    te::MidiInputDevice* getOrCreateKeyboardDevice (te::Engine& engine);

    /**
     * Note ueber das virtuelle Geraet spielen. Liefert false, wenn es das Geraet
     * nicht gibt — dann muss der Aufrufer auf den Vorhoer-Weg zurueckfallen,
     * damit man wenigstens etwas hoert.
     */
    bool sendKeyboardNoteOn  (te::Engine& engine, int pitch, int velocity);
    bool sendKeyboardNoteOff (te::Engine& engine, int pitch);

    /**
     * Das Keyboard auf diese Spur legen: Geraet als Eingang zuweisen und
     * Monitoring einschalten. So hoert man beim Spielen das Instrument der Spur
     * und der Aufnahmeknopf nimmt genau das auf.
     *
     * Wirkt wie Logics "die ausgewaehlte Instrumentenspur hoert mit": beim
     * Wechsel auf eine andere Instrumentenspur wandert die Zuweisung mit.
     */
    void routeKeyboardToTrack (te::AudioTrack& track);

    /**
     * Instrumentenspur anlegen: Spur ans Ende, Merkmal, leeren MIDI-Clip (damit
     * sofort etwas zum Doppelklicken da ist), Keyboard zuweisen — und dazwischen
     * das Instrument, das der Aufrufer einsetzt. EIN Undo-Schritt.
     *
     * Das Einsetzen kommt als Rueckruf herein, damit dieser Kern nicht am
     * PluginManager haengt (der wiederum am Scanner haengt) und headless
     * testbar bleibt.
     */
    te::AudioTrack* createInstrumentTrack (te::Edit& edit,
                                           const juce::String& trackName,
                                           std::function<void (te::AudioTrack&)> insertInstrument);
}
