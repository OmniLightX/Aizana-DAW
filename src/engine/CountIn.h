#pragma once

#include "EditManager.h"
#include <functional>

namespace te = tracktion::engine;

//==============================================================================
/**
    Vorzaehler / Pre-Roll — NAHTLOS.

    ## Was ein Vorzaehler fachlich IST

    Studio One: „a count-off of the length indicated in the adjacent Bars field
    occurs when the Record button is clicked."
    Logic Pro:  „playback begins before the playhead position. When reaching the
    playhead position, Logic switches to record mode."

    Beide Formulierungen sagen dasselbe: es gibt **einen** durchgehenden Lauf.
    Der Saenger holt Luft, der Beatbauer zaehlt mit — und an der Kante beginnt
    die Aufnahme, ohne dass irgendetwas anhaelt. Ein Vorzaehler, der stoppt und
    neu startet, ist kein Vorzaehler; er ist genau das, wofuer man ihn abschafft.

    ## Warum die beiden Vorgaenger-Anlaeufe scheiterten (2026-08-16)

    1. **Anlauf 1 (Punch, unverkapselt):** setzte Loop-Bereich UND
       `recordingPunchInOut` des ANWENDERS um und stellte sie nicht wieder her.
       Stand die Schleife des Anwenders an, verweigerte TE die Aufnahme
       ueberhaupt (`TransportControl.cpp`: „Recording can be done in either loop
       mode or punch in/out mode, but not both") — der Kopf lief vor und es
       passierte nie etwas.
    2. **Anlauf 2 (Timer + stop/setPosition/record):** hoerbare NAHT am
       Umschaltpunkt, und der Aufnahmestart lag um bis zu eine Timer-Periode
       (30 ms) daneben — beides fachlich unbrauchbar.

    ## Wie es wirklich geht — TE kann das, man muss es nur richtig fragen

    `TransportControl::performRecord()` hat einen Zweig fuer „laeuft schon":

        if (isPlaying()) {
            punchInTime = edit.recordingPunchInOut ? getLoopRange().getStart()
                                                   : playbackContext->getPosition();
            playbackContext->prepareForRecording (currentPos, punchInTime);
        }

    Das ist der ganze Trick: **Aufnahme scharfschalten, WAEHREND der Transport
    laeuft.** Es wird nichts gestoppt und nichts umgesetzt — TE armiert nur die
    Eingaenge und punched an `punchInTime` ein. Mit `recordingPunchInOut` ist
    dieser Punkt **sample-genau** der Anfang des Punch-Bereichs, voellig
    unabhaengig davon, wann wir scharfgeschaltet haben. Genau deshalb duerfen
    wir gemuetlich aus einem 30-ms-Timer heraus armieren, ohne einen einzigen
    Sample Genauigkeit zu verlieren.

    Damit der Anwender davon nichts merkt, ist der Punch-Zustand **gekapselt**:
    Loop-Bereich, Punch-Schalter und Klickbereich werden gesichert, gesetzt und
    restlos zurueckgegeben — auch bei Abbruch und auch, wenn TE die Aufnahme
    ablehnt. Wer waehrenddessen selbst am Loop-Bereich zieht, behaelt seinen
    Bereich (wir schreiben nur zurueck, was noch unser eigener Wert ist).

    ## Der eine Sonderfall: Schleifen-Aufnahme (Takes)

    Bei eingeschalteter Schleife kann der Abspielkopf gar nicht VOR dem Bereich
    stehen — `PlayHead::referenceSamplePositionToTimelinePosition()` faltet jede
    Position modulo in den Bereich zurueck, solange `rollInToLoop` nicht gesetzt
    ist. Und dieses Flag setzt ausschliesslich TEs eigener Aufnahmeweg. Deshalb
    uebernimmt fuer Schleifen-Aufnahmen TEs nativer Vorzaehler
    (`Edit::setCountInMode`) — er rollt korrekt in die Schleife hinein und
    laesst die Take-Mechanik (B5) unberuehrt. Sein Vorrat ist 1 oder 2 Takte;
    vier Takte kennt TE nicht.

    Das sind bewusst zwei Wege, aber sie haengen nicht an einem versteckten
    dritten Zustand: der Weg wird ausschliesslich vom SICHTBAREN Schleifen-
    Schalter gewaehlt, und beide Wege sind fuer sich vollstaendig.
*/
namespace countin
{
    /** Der geplante Vorlauf. Reine Rechnung auf der Tempospur — golden-testbar. */
    struct Plan
    {
        bool   enabled          = false;  ///< false = kein Platz / abgeschaltet
        double countInStartSecs = 0.0;    ///< hier setzt die Wiedergabe ein
        double recordStartSecs  = 0.0;    ///< hier punched die Aufnahme ein

        double lengthSecs() const noexcept { return recordStartSecs - countInStartSecs; }
    };

    /** Was der Klick waehrend eines Aufnahmestarts tun soll.

        TEs `ClickGenerator::isMutedAtTime()` kennt genau zwei Stellschrauben:
        den Schalter `clickTrackEnabled` und einen Klickbereich. Der Bereich
        wirkt NUR, solange der Schalter AUS ist (`if (! clickEnabled) return
        ! range.contains (time);`) — steht der Schalter an, klingt der Klick
        ueberall und der Bereich ist wirkungslos. Genau daran hing der Befund
        vom 2026-08-17: wer das Metronom eingeschaltet hatte, hoerte es durch
        die ganze Aufnahme, egal was der Vorzaehler setzte. */
    struct ClickSetup
    {
        bool enabled = false;                  ///< Wert fuer `clickTrackEnabled`
        tracktion::TimeRange range;            ///< leer = kein Bereich
    };

    /** Die Regel, nach der Logic Pro und Studio One den Klick beim Aufnehmen
        behandeln — hier als reine Funktion, damit sie golden-testbar ist.

        Recherchiert am 2026-08-17:
          * Logic Pro: „Click while playing" (= unser Metronom-Knopf),
            „Click while recording" und darunter „Only during count-in" —
            letzteres ist ausdruecklich als Unterpunkt von „Click while
            recording" definiert.
          * Studio One / Fender Studio Pro: „Click in play" und „Click in
            precount only" im Metronom-Fenster.

        Beide trennen also **Wiedergabe** und **Aufnahme**, und beide kennen den
        Fall „zaehlt vor, schweigt dann". Ohne diese Trennung ist Gesang nicht
        aufnehmbar; mit permanentem Klick ist Beatbau nicht moeglich. Es sind
        zwei verschiedene Paar Schuhe.

        `clickWhileRecording` ist bewusst ein MODIFIKATOR, keine eigene Quelle:
        es entscheidet, ob der Klick des Anwenders ab dem Aufnahmestart
        WEITERlaeuft. Steht das Metronom aus, bleibt die Aufnahme still — sonst
        finge es beim Aufnehmen ploetzlich von selbst an zu klacken.

        @param userClickOn         Metronom-Knopf des Anwenders
        @param countInClick        Vorzaehler bestellt (Pre-Roll: false)
        @param clickWhileRecording „Klick bei Aufnahme" aus den Einstellungen
        @param runUp               Vorlaufbereich (leer = kein Vorlauf) */
    ClickSetup decideClick (bool userClickOn, bool countInClick, bool clickWhileRecording,
                            tracktion::TimeRange runUp);

    /** „Klick bei Aufnahme" — liegt in der SESSION (wie `clickTrackEnabled`),
        nicht in einer globalen Einstellung. Vorgabe: an (Logics Vorgabe, und
        der Beatbau-Fall). */
    bool getClickWhileRecording (te::Edit&);
    void setClickWhileRecording (te::Edit&, bool);

    /** Vorlauf-Anfang: `recordStart` wird auf seine Taktlinie ABGERUNDET, von dort
        gehen `bars` ganze Takte zurueck.

        Damit gilt beides gleichzeitig, und genau das macht einen Vorzaehler
        brauchbar:
          * der Vorlauf beginnt IMMER auf einer Eins (der Klick zaehlt im Takt
            des Songs, nicht irgendwo dazwischen), und
          * er endet IMMER exakt am Aufnahmestart.
        Liegt der Aufnahmestart mitten im Takt, ist der Vorlauf entsprechend
        laenger als `bars` — nie kuerzer.

        Der Anfang darf VOR dem Songanfang liegen (negative Zeit). Das ist kein
        Schoenheitsfehler, sondern der Fix fuer den haeufigsten Fall ueberhaupt:
        der Vorgaenger klemmte mit `jmax (0, bars - countInBars)` auf Takt 1 —
        wer am Songanfang aufnahm (Beatbau!), bekam damit GAR KEINEN Vorzaehler.
        TE rechnet und spielt in negativer Zeit; es klemmt beim Positionieren
        lediglich auf `-getNumCountInBeats()` Beats `- 0.5 s`, und diese Klemme
        wird beim Anwenden beruecksichtigt, nicht hier. */
    Plan makePlan (te::TempoSequence&, tracktion::TimePosition recordStart, int bars);
}

//==============================================================================
/**
    Fuehrt den Vorlauf aus und raeumt hinter sich auf.

    Lebt in der TransportBar, wird von `recordClicked()` gestartet und von deren
    Timer gepollt. ALLE Methoden laufen auf dem Message-Thread (I-02).
*/
class CountInRecorder
{
public:
    enum class Outcome
    {
        noRunUp,          ///< kein Vorlauf noetig/moeglich — Aufrufer nimmt normal auf
        rollingIn,        ///< Wiedergabe laeuft, Aufnahme wird gleich scharfgeschaltet
        nativeLoopRollIn, ///< TEs eigener Vorzaehler rollt in die Schleife — fertig
        failed            ///< TE hat die Aufnahme abgelehnt; Zustand ist zurueckgegeben
    };

    /** Message-thread preparation immediately before each actual record call,
        including the delayed start after pre-roll. Must not replace the Edit. */
    std::function<void()> beforeRecording;

    /** Startet den Vorlauf.

        @param bars                Takte Vorlauf (>0)
        @param countInClick        true = Klick fuer die Dauer des Vorlaufs
                                   erzwingen (Vorzaehler); false = Klick so
                                   lassen, wie er ist (Pre-Roll).
        @param clickWhileRecording false = der Klick hoert am Aufnahmestart auf
                                   (`countin::decideClick`).
        @returns                   was passiert ist — bei `noRunUp` muss der
                                   Aufrufer selbst `transport.record()` rufen,
                                   sonst NICHT. */
    Outcome start (te::Edit&, int bars, bool countInClick, bool clickWhileRecording);

    /** Fuer die Aufnahme OHNE Vorlauf: haelt den Klick ab dem Aufnahmestart
        still, wenn „Klick bei Aufnahme" aus ist, und gibt ihn beim Stoppen
        zurueck. Erst NACH `transport.record()` rufen — dann ist der geliehene
        Zustand an die laufende Aufnahme gebunden. */
    void holdClickForRecording (te::Edit&, bool clickWhileRecording);

    /** Aus dem Transport-Timer. Schaltet die Aufnahme scharf, sobald der
        Transport wirklich rollt, und gibt den geliehenen Zustand zurueck,
        sobald die Aufnahme endet oder abgebrochen wird. */
    void poll (te::Edit&);

    /** Der Edit verschwindet (Session-Wechsel, I-11): geliehenen Zustand
        vergessen, ohne ihn noch anzufassen. */
    void detach() noexcept;

    bool isActive() const noexcept       { return phase != Phase::idle; }

    /** true, solange WIR den Punch-Schalter halten — die Transportleiste zeigt
        dann weiter den Wert des Anwenders an, damit der Auto-Punch-Knopf nicht
        von selbst aufleuchtet. */
    bool ownsPunch() const noexcept      { return borrowedPunch; }
    bool userPunchValue() const noexcept { return savedPunch; }

    /** Dasselbe fuer den Klick-Schalter: waehrend wir ihn halten (z. B. „Klick
        bei Aufnahme AUS" bei eingeschaltetem Metronom) zeigt die
        Transportleiste weiter den Wert des ANWENDERS — sonst ginge sein
        Metronom-Knopf beim Aufnehmen sichtbar aus und danach wieder an. */
    bool ownsClick() const noexcept      { return borrowedClick; }
    bool userClickValue() const noexcept { return savedClick; }

private:
    enum class Phase { idle, rollingIn, recording };

    void giveBack (te::Edit&);

    Phase phase = Phase::idle;

    void borrowClick (te::Edit&, countin::ClickSetup);

    bool borrowedPunch = false;   ///< haben wir Loop-Bereich + Punch geliehen?
    bool savedPunch    = false;   ///< Punch-Schalter des Anwenders

    bool borrowedClick = false;   ///< haben wir den Klick-Schalter geliehen?
    bool savedClick    = false;   ///< Metronom-Knopf des Anwenders
    bool sawRecording  = false;   ///< die Aufnahme lief wirklich an
    tracktion::TimeRange savedLoopRange, appliedLoopRange;

    tracktion::TimePosition runUpStart, recordStart;
};
