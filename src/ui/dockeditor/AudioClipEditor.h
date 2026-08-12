#pragma once

#include <map>
#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>
#include "engine/EditManager.h"
#include "engine/ClipEditor.h"
#include "engine/AutomationEngine.h"
#include "ui/automation/AutomationLane.h"
#include "ui/common/AudioThumbnailPool.h"
#include "EditorViewState.h"

class CompingEngine;
class CompingView;

namespace te = tracktion::engine;

/**
 * AudioClipEditor — large waveform editor for ONE WaveAudioClip, docked at the
 * bottom (Studio One "Edit" view). Operates directly on the live clip object.
 *
 * S1-Parität (docs/EDITOR_PARITY_S1.md, E1–E5): the editor is a full WORKPLACE,
 * not a display — same tool model as the arrangement:
 *
 *   - Tools: Pfeil (1) · Split (2/C) · Range (3, oder Cmd+Drag wie im Arrange)
 *   - Split-Tool-Klick/Doppelklick = Schnitt an (gesnappter) Position
 *   - Range aufziehen → Delete/Backspace entfernt den Bereich aus dem Clip in
 *     EINEM Undo-Schritt (ClipEditor::removeRangeFromClip — der Atmer-Workflow)
 *   - Snap an 1/16-Raster (Toggle "Snap" / Taste N, Alt = temporär aus)
 *   - Clip-Mute-Toggle (I-03: ValueTree-Property über den UndoManager)
 *   - Automation-Lane (Volume/Pan, AutomationLane-Reuse) unter der Waveform
 *   - Edge-Trim, Fades, Clip-Gain, Zoom/Scroll wie gehabt (Pfeil-Werkzeug)
 *   - E6: Takes/Comping — bei Loop-Takes zeigt "Takes" die Studio-One-Lanes
 *     (CompingView-Reuse) direkt im Dock; Lane-Drag = Comp-Bereich, Klick =
 *     Section wechseln, Label-Klick = ganzer Take (CompingEngine, je 1 Undo)
 *
 * All mutations go through the Edit's shared UndoManager — one transaction per
 * gesture. NO copied clip data: the arrangement shows every change immediately
 * via its own ValueTree listeners.
 */
class AudioClipEditor : public juce::Component,
                        private juce::ValueTree::Listener,
                        private juce::Timer,
                        private juce::ScrollBar::Listener
{
public:
    AudioClipEditor (te::WaveAudioClip& clip, EditManager& editManager,
                     CompingEngine* compingEngine = nullptr);
    ~AudioClipEditor() override;

    te::Clip& getClip() noexcept { return clip; }

    /**
     * S1-Anker (Studio-One-Parität): Zeitachse + Werkzeug-Zustand überleben den
     * Selektionswechsel. DockedEditorPanel liest den Handover vom alten Editor
     * und wendet ihn auf den neuen an — die Edit-View "springt" dadurch nicht
     * mehr, wenn nach einem Split das andere Teilstück selektiert wird.
     */
    struct ViewHandover
    {
        double absStart = 0.0;             // absolute Sekunden am linken Fensterrand
        double len      = 0.0;             // Fensterlänge in Sekunden
        int    tool     = 0;               // (int) Tool
        bool   snap     = true;
        bool   automationLaneOpen = false;
        bool   takesOpen          = false; // E6: Takes-Lanes überleben den Wechsel …
        bool   takesCapable       = false; // … aber nur Takes-Clip → Takes-Clip (sonst
                                           // würde ein takes-loser Clip den Auto-Default
                                           // des nächsten Multi-Take-Clips zuschließen)
    };

    ViewHandover getViewHandover() const;
    void applyViewHandover (const ViewHandover& h);

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;   // I-05: Range-Overlay
    void resized() override;

    void mouseDown  (const juce::MouseEvent& e) override;
    void mouseDrag  (const juce::MouseEvent& e) override;
    void mouseUp    (const juce::MouseEvent& e) override;
    void mouseMove  (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify   (const juce::MouseEvent& e, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    te::WaveAudioClip& clip;
    EditManager&       editManager;
    ClipEditor         clipEditor;        // engine service (stateless over EditManager)
    AutomationEngine   automationEngine;  // engine service for the automation lane

    // ---- Header widgets ----
    juce::Label  nameLabel;
    juce::Label  gainCaption;
    juce::Slider gainSlider;      // clip gain in dB (te::IDs::gain)
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomFitButton { "Fit" };

    // ---- Toolbar (S1-Muster: Werkzeuge + Snap + Aktionen) ----
    enum class Tool { pointer, split, range };
    Tool activeTool  = Tool::pointer;
    bool snapEnabled = true;

    /**
     * Unsichtbarer, klick-durchlaessiger Marker ueber waveArea().
     *
     * Die Wellenflaeche ist ein BERECHNETES Rechteck, keine Component — damit war sie
     * fuer Namens-Zugriffe (Bedienungshilfen, GUI-Testlaeufe) nicht ansprechbar, und
     * Gesten haetten auf Bildschirmkoordinaten gezielt, die bei jedem Zoom, Scroll oder
     * eingeblendeter Automations-/Takes-Spur woanders liegen. Der Marker zeichnet nichts
     * und faengt nichts ab (setInterceptsMouseClicks(false,false)); alle Ereignisse gehen
     * weiter an den Editor.
     */
    juce::Component waveAreaMarker;

    juce::TextButton toolPointerBtn { juce::String::fromUTF8 ("Pfeil") };
    juce::TextButton toolSplitBtn   { "Split" };
    juce::TextButton toolRangeBtn   { "Range" };
    juce::TextButton snapBtn        { "Snap" };
    juce::TextButton muteBtn        { "Mute" };
    juce::TextButton autoBtn        { "Auto" };
    juce::TextButton takesBtn       { "Takes" };
    juce::TextButton gainEnvBtn     { "Env" };
    juce::TextButton bendBtn        { "Bend" };
    juce::TextButton rangeCutBtn    { juce::String::fromUTF8 ("\xe2\x88\x92 Bereich") };

    void setTool (Tool t);
    void updateToolbarState();

    // ---- P5: Clip-Gain-Envelope (Multi-Point-Gain, ClipGainEnvelope-Engine) ----
    // Env-Modus (Taste G): Klick in die Waveform = Punkt setzen, Drag = ziehen,
    // Doppelklick auf Punkt = löschen. Kurve wird IMMER gezeichnet, wenn sie
    // existiert; editierbar nur im Env-Modus. 1 Undo-Transaktion pro Geste.
    bool gainEnvMode  = false;
    int  envDragPoint = -1;                    // Kurvenpunkt-Index während des Drags

    static constexpr float kEnvTopDb    = 6.0f;    // y-Mapping der Envelope-Anzeige
    static constexpr float kEnvBottomDb = -30.0f;  // unterer Rand = -30 dB (Drag ganz
                                                   // unten = kill, Engine clampt -60)

    float dbToEnvY (float db, const juce::Rectangle<int>& wa) const;
    float envYToDb (float y,  const juce::Rectangle<int>& wa) const;
    int   envPointAt (juce::Point<int> pos, const juce::Rectangle<int>& wa);
    void  paintGainEnvelope (juce::Graphics& g, const juce::Rectangle<int>& wa);

    // ---- P7b: Audio-Bend/Warp (WarpEngine über TEs WarpTimeManager) ----
    // Bend-Modus (Taste B): Klick = Warp-Marker setzen (snappt auf erkannte
    // Transienten), Marker ziehen = Timing biegen (Ghost-Linie, Commit auf
    // mouseUp = 1 Undo + 1 Warp-Render), Doppelklick = Marker löschen,
    // Rechtsklick = Zurücksetzen/Deaktivieren. Marker sichtbar sobald Warp
    // aktiv ist; editierbar nur im Bend-Modus (P5-Env-Konvention).
    bool  bendMode       = false;
    int   bendDragMarker = -1;      // Marker-Index während des Drags
    float bendDragX      = 0.0f;    // Ghost-Position (Commit erst auf mouseUp)

    void   toggleBendMode();
    int    bendMarkerAt (juce::Point<int> pos, const juce::Rectangle<int>& wa);
    void   paintWarpMarkers (juce::Graphics& g, const juce::Rectangle<int>& wa);
    double warpToLocal (double warpSecs) const;    // Warp-Zeit ↔ Clip-Lokalzeit
    double localToWarp (double localSecs) const;   // (Offset-Verschiebung)

    // ---- Range selection (clip-LOCAL seconds) ----
    bool   rangeActive   = false;
    bool   rangeDragging = false;
    double rangeA = 0.0, rangeB = 0.0;

    void applyRangeDelete();
    void splitAt (double localSecs);

    // Snap: 1/16-Raster über die TempoSequence; Alt = bypass.
    double snapLocal (double localSecs, bool bypassSnap) const;

    bool isClipMuted() const;
    void toggleClipMute();

    // ---- Takes/Comping lanes (E6, CompingView-Reuse im Dock) ----
    CompingEngine* compingEngine = nullptr;   // nullptr = Takes-UI aus (Tests)
    bool takesVisible = false;
    std::unique_ptr<CompingView> takesView;
    void toggleTakesLanes();
    int  takesAreaHeight() const;             // 0 wenn zu / keine Takes

    // ---- Automation lane (AutomationLane-Reuse, arrangement-style mapping) ----
    bool automationVisible = false;
    std::unique_ptr<AutomationLane> autoLane;
    double lanePixelsPerSecond = 100.0;   // by-ref view mapping for AutomationLane
    double laneScrollSeconds   = 0.0;     // absolute seconds at lane x=0
    void toggleAutomationLane();
    void updateLaneMapping();

    // ---- View state: window into the clip, in clip-LOCAL seconds [0, len] ----
    double viewStartSecs = 0.0;
    double viewLenSecs   = 0.0;   // 0 = uninitialised → fit whole clip on first paint
    juce::ScrollBar hScroll { false };

    juce::SharedResourcePointer<AudioThumbnailPool> thumbnailPool;
    juce::AudioThumbnail thumbnail { 512, thumbnailPool->formatManager, thumbnailPool->cache };

    // Nachbar-Clips derselben Spur werden gedimmt mitgezeichnet (S1: die Edit-
    // View zeigt die Spur im Zeitkontext — nach einem Split "verschwindet" das
    // rechte Teilstück nicht). Thumbnails lazy pro Quelldatei, App-weiter Cache.
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> neighbourThumbs;
    juce::AudioThumbnail* neighbourThumbFor (const juce::File& sourceFile);
    void paintNeighbourClips (juce::Graphics& g, const juce::Rectangle<int>& wa);

    // ---- Gesture state ----
    enum class Drag { none, trimLeft, trimRight, fadeIn, fadeOut };
    Drag  activeDrag = Drag::none;
    int   dragStartScreenX     = 0;
    double dragStartClipStart  = 0.0;
    double dragStartClipEnd    = 0.0;
    double dragStartOffset     = 0.0;
    double dragStartFadeSecs   = 0.0;
    double trimMaxExtendLeft   = 0.0;
    double trimMaxExtendRight  = 0.0;
    double dragStartViewAbs    = 0.0;   // S1-Anker: absolute Fensterlage beim Trim halten

    static constexpr int kHeaderH   = 28;
    static constexpr int kToolbarH  = 26;
    static constexpr int kAutoLaneH = 96;
    static constexpr int kScrollH   = 12;
    static constexpr int kEdgeZone  = 10;   // px trim zones
    static constexpr int kFadeGrab  = 14;   // px fade handle hit size

    juce::Rectangle<int> waveArea() const;
    double clipLenSecs() const;
    double sourceLenSecs();   // non-const: getAudioFile() is non-const in TE
    double clipStartAbsSecs() const { return clip.getPosition().getStart().inSeconds(); }

    // clip-local time ↔ x pixel inside waveArea
    double xToLocalSecs (float x) const;
    float  localSecsToX (double t) const;

    void clampView();
    void zoomAround (double factor, float pivotX);
    void syncScrollBar();
    void syncGainSlider();

    // ValueTree::Listener — repaint on any clip property change (undo, arrangement edits)
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded   (juce::ValueTree&, juce::ValueTree&) override {}
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override {}
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}

    void timerCallback() override;                     // playhead repaint
    void scrollBarMoved (juce::ScrollBar*, double) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioClipEditor)
};
