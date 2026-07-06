#pragma once
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * ClipGainEnvelope — P5 (S1-Parität): Multi-Point-Gain-Kurve pro Audio-Clip.
 *
 * Mechanik: ein VERSTECKTES te::VolumeAndPanPlugin in der CLIP-eigenen
 * PluginList (Event-FX-Pfad, TE spielt Clip-Plugins live); die Gain-Kurve ist
 * die Automation-Kurve von dessen volParam. Der Plugin-Typ "volume" ist in
 * allen User-Plugin-Filtern (Insert-Slots, Event-FX-Menü) bereits ausgeblendet
 * → der Träger bleibt unsichtbar. TE evaluiert Plugin-Automation im
 * Playback-Graph auch für Clip-Plugins — render-bewiesen in [gainenv].
 *
 * Zeitbasis: Kurvenpunkte liegen in EDIT-Zeit (TE-Konvention für Automation).
 * Operationen, die den Clip VERSCHIEBEN (Move/Paste/Copy), müssen shift() mit
 * demselben Delta in DERSELBEN Undo-Transaktion aufrufen — ClipEditor tut das.
 * Split braucht keine Zeitkorrektur: die Kurve wird auf die rechte Hälfte
 * kopiert (copyFrom), beide Hälften spielen nur ihren Abschnitt → der
 * Gain-Verlauf über die Zeit bleibt identisch.
 *
 * Werte: UI spricht dB, intern Fader-Position (te::decibelsToVolumeFaderPosition).
 *
 * Undo (I-02): addPoint/movePoint/removePoint/clear eröffnen KEINE eigene
 * Transaktion — der Aufrufer setzt die Undo-Grenze (UI: 1 Transaktion pro
 * Geste; ClipEditor: Teil der Clip-Operation). Transaktionsfreie Cores wie
 * bei StemImport/Bridge.
 */
struct ClipGainEnvelope
{
    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 6.0f;

    /** Der versteckte Envelope-Träger, nullptr wenn keiner existiert. */
    static te::VolumeAndPanPlugin* find (te::WaveAudioClip& clip);

    /** Träger holen oder anlegen (Anlegen gehört in die Transaktion des Aufrufers). */
    static te::VolumeAndPanPlugin* getOrCreate (te::WaveAudioClip& clip);

    /** volParam des Trägers, nullptr wenn kein Träger. */
    static te::AutomatableParameter* param (te::WaveAudioClip& clip);

    /** true wenn ein Träger mit >= 1 Kurvenpunkt existiert. */
    static bool hasEnvelope (te::WaveAudioClip& clip);

    static int    numPoints (te::WaveAudioClip& clip);
    static double pointTime (te::WaveAudioClip& clip, int index);   // Edit-Sekunden
    static float  pointDb   (te::WaveAudioClip& clip, int index);

    /** Punkt hinzufügen (legt den Träger bei Bedarf an). Gibt den Punkt-Index zurück. */
    static int  addPoint (te::WaveAudioClip& clip, double editTimeSecs, float gainDb);

    /** Punkt verschieben (Drag). removeIntervening=true räumt übersprungene Punkte weg. */
    static int  movePoint (te::WaveAudioClip& clip, int index,
                           double editTimeSecs, float gainDb);

    static void removePoint (te::WaveAudioClip& clip, int index);

    /** Ganze Hüllkurve entfernen (Kurve leeren + Träger-Plugin aus dem Clip). */
    static void clear (te::WaveAudioClip& clip);

    /** Alle Punkte um deltaSecs verschieben — für Clip-Move/Copy/Paste. */
    static void shift (te::WaveAudioClip& clip, double deltaSecs);

    /** Kurve von src auf dest kopieren (Split: rechte Hälfte erbt den Verlauf). */
    static void copyFrom (te::WaveAudioClip& dest, te::WaveAudioClip& src);

    /** Gain in dB zur Edit-Zeit (0.0 wenn keine Hüllkurve). Für UI-Zeichnung. */
    static float gainDbAt (te::WaveAudioClip& clip, double editTimeSecs);
};
