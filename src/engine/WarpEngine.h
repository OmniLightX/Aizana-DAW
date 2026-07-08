#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

/**
 * WarpEngine — P7b Audio-Bend/Warp (Studio One "Audio Bend") über TEs
 * WarpTimeManager.
 *
 * Ein Warp-Marker mappt eine Quell-Zeit (Position in der Audiodatei) auf eine
 * Warp-Zeit (Position auf der gedehnten Zeitachse). Zwischen Markern stretcht
 * TE linear (Ratio von TE geclampt auf 0.1..20).
 *
 * PLAYBACK-PFAD (wichtiger TE-Fund): Für plain zeitbasierte Clips läuft Warp
 * über den PROXY-Pfad — jede Markeränderung feuert clip.changed(), TE rendert
 * die gewarpte Datei im Hintergrund neu und spielt sie als Proxy. Der
 * Realtime-WarpMap-Zweig (WaveNodeRealTime) existiert NUR für beats-basierte
 * Clips (autoTempo/autoPitch/Launcher) — der zeitbasierte
 * WaveNodeRealTime-Ctor hat keinen WarpMap-Parameter. Deshalb Proxy AN
 * lassen (Default): hörbar nach kurzem BG-Render statt sofort; Offline-
 * Render/Bounce WARTET korrekt auf den Proxy (WaveNode::isReadyToProcess
 * blockiert bei forRendering bis der Reader existiert).
 *
 * ⚠️ TE-BUG-UMSCHIFFUNG (real gefangen: Warp-Render war STILLE): TEs eigener
 * WarpTimeRenderJob baut seine Segment-Liste über clip.getWaveInfo() — aber
 * updateSourceFile() hat die Clip-Quelle da schon auf die (noch nicht
 * existierende) Zieldatei gesetzt. getWaveInfo() hat für diesen Fall nur
 * einen Project-Item-Fallback; Aizana nutzt direkte File-Referenzen (I-07)
 * → sampleRate 0 → leere Segmente → stiller Render. Deshalb registriert
 * kickWarpRender() VOR TEs 600ms-Proxy-Timer einen eigenen Job auf demselben
 * Ziel-Hash, dessen Segment-Liste aus der ORIGINALDATEI gebaut wird
 * (AudioSegmentList::create(clip, wtm, sourceFile)); TEs renderSource()
 * ADOPTIERT ihn dann über getRenderJobWithoutCreating(). Der
 * WarpRenderWatcher (EditManager) feuert kickWarpRender bei JEDER
 * Warp-Änderung im Edit — auch Undo/Redo bei geschlossenem Editor.
 *
 * Alle Methoden sind TRANSAKTIONSFREIE Cores (WarpTimeManager schreibt durch
 * den Edit-UndoManager) — der Aufrufer öffnet genau EINE Transaktion pro
 * User-Geste (UI-Konvention wie ClipEditor).
 */
class WarpEngine
{
public:
    struct Marker
    {
        double sourceSecs = 0.0;   // Position in der Quelldatei
        double warpSecs   = 0.0;   // Position auf der gewarpten Zeitachse
    };

    /** True, wenn Audio-Bend auf dem Clip aktiv ist. */
    static bool isWarped (const te::WaveAudioClip& clip);

    /**
     * Audio-Bend an-/abschalten. Beim Einschalten: Timestretch-Modus wird
     * gesetzt falls nötig (SoundTouch) und der WarpTimeManager seedet die
     * Endpunkt-Marker (0 und Quelllänge). Beim Ausschalten spielt der Clip
     * wieder linear (TE stellt die Original-Quelle wieder her); die Marker
     * bleiben im State erhalten (erneutes Einschalten stellt das Bend wieder
     * her).
     */
    static void setWarpEnabled (te::WaveAudioClip& clip, bool enabled);

    /** Alle Marker, nach warpSecs sortiert (TE hält die Ordnung). */
    static juce::Array<Marker> getMarkers (const te::WaveAudioClip& clip);

    /**
     * Neuen Marker an einer Quell-Zeit anlegen. Die Warp-Zeit wird über das
     * aktuelle Mapping bestimmt — das Anlegen selbst ändert den Klang NICHT
     * (identisches Mapping), erst das Verschieben. Gibt den Marker-Index
     * zurück, oder -1 wenn Warp nicht aktiv ist.
     */
    static int addMarkerAtSourceTime (te::WaveAudioClip& clip, double sourceSecs);

    /**
     * Marker auf eine neue Warp-Zeit ziehen. TE clampt die Stretch-Ratio der
     * Nachbar-Segmente (0.1..20). Gibt die tatsächlich gesetzte Warp-Zeit
     * zurück.
     */
    static double moveMarker (te::WaveAudioClip& clip, int index, double newWarpSecs);

    /** Marker entfernen. Endpunkt-Marker werden von TE zurückgesetzt statt
        entfernt (Anfang/Ende brauchen immer einen Anker). */
    static void removeMarker (te::WaveAudioClip& clip, int index);

    /** Alle Marker auf die Endpunkte zurücksetzen (Bend neutral). */
    static void resetMarkers (te::WaveAudioClip& clip);

    /** Warp-Zeit → Quell-Zeit über die aktuelle Marker-Kette. */
    static double warpToSource (const te::WaveAudioClip& clip, double warpSecs);

    /** Quell-Zeit → Warp-Zeit über die aktuelle Marker-Kette. */
    static double sourceToWarp (const te::WaveAudioClip& clip, double sourceSecs);

    /**
     * Transienten der Quelldatei (TE-Detection-Job, läuft im Hintergrund).
     * first=false solange der Job noch rechnet — periodisch erneut abfragen.
     */
    static std::pair<bool, juce::Array<double>> getTransients (const te::WaveAudioClip& clip);

    /**
     * Warp-Render für den AKTUELLEN Marker-Stand anstoßen (Message-Thread).
     * Registriert einen korrekten Render-Job (Segment-Liste aus der
     * Originaldatei, siehe Klassen-Doku) und lässt den Clip ihn adoptieren.
     * Idempotent: existiert die Ziel-Datei oder läuft schon ein Job, no-op.
     */
    static void kickWarpRender (te::WaveAudioClip& clip);

    /**
     * True, solange der Warp-Render für den aktuellen Marker-Stand noch nicht
     * fertig ist (Job registriert ODER Zieldatei noch nicht als Clip-Quelle
     * valid). Die UI kann daraus eine „Warping…"-Anzeige speisen; Tests warten
     * damit deterministisch auf den ABSCHLUSS (nicht nur auf „Datei existiert" —
     * AudioFile::isValid() wird schon true, bevor der BG-Render die Samples
     * geschrieben hat).
     */
    static bool isWarpRenderPending (const te::WaveAudioClip& clip);

private:
    WarpEngine() = delete;
};

/**
 * WarpRenderWatcher — beobachtet WARPMARKER-/warpTime-Änderungen im ganzen
 * Edit (inkl. Undo/Redo, auch bei geschlossenem Editor) und stößt den
 * korrekten Warp-Render an, BEVOR TEs eigener (stiller, siehe WarpEngine-
 * Doku) 600ms-Timer-Job zum Zug kommt. EditManager hält genau einen und
 * re-attacht ihn bei jedem Edit-Wechsel.
 */
class WarpRenderWatcher : private juce::ValueTree::Listener
{
public:
    WarpRenderWatcher() = default;
    ~WarpRenderWatcher() override { detach(); }

    /** Am neuen Edit anmelden (löst ein bestehendes Attach vorher). */
    void attach (te::Edit& edit);

    /** Vor der Zerstörung des Edits abmelden (I-11). */
    void detach();

private:
    te::Edit* edit = nullptr;
    std::shared_ptr<int> aliveToken;   // invalidiert ausstehende callAsyncs beim Detach

    void scheduleKick (juce::ValueTree treeInsideClip);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WarpRenderWatcher)
};
