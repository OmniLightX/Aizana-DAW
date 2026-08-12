#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/*
    AIZANA_UITEST_HOOK — In-App-Maussteuerung fuer GUI-Testlaeufe.

    Wird ueber die CMake-Option AIZANA_UITEST_HOOK=ON eingeschaltet und ist im
    ausgelieferten Build NICHT VORHANDEN: ohne die Option wird UiTestHook.cpp gar
    nicht uebersetzt und dieses Header stellt nur ein constexpr-false bereit, das
    der Compiler an jeder Aufrufstelle wegfaltet. Nachweisbar am Artefakt selbst
    ueber scripts/uitest-guard-check.sh (Symboltabelle).

    WARUM DIESER WEG: der Hook speist synthetische Mausereignisse an genau der
    Stelle ein, an der macOS die echten einspeist —
    juce::ComponentPeer::handleMouseEvent (juce_NSViewComponentPeer_mac.mm,
    sendMouseEvent). Diese Signatur traegt KEIN Ziel: sie nimmt einen Punkt,
    Modifier, Druck und Zeit; adressiert wird das Objekt, auf dem die Methode
    gerufen wird, und ComponentPeer::getPeer()/getNumPeers() zaehlen ausschliesslich
    Fenster DIESES Prozesses. Es gibt in der Kette keinen Parameter, ueber den ein
    fremder Prozess benannt werden koennte. Kein Systemrecht (kein Event-Tap, keine
    Bedienungshilfen-Freigabe), kein Fokusklau, kein Fehlklick in Finder, Banner
    oder Berechtigungsdialog — die Ereignisse betreten den Bildschirm nie.

    Die Steuerleitung ist bewusst eine UNBENANNTE PIPE auf stdin: kein Pfad, kein
    Port, kein Eintrag in irgendeinem Namensraum, nur vom startenden Prozess
    beschreibbar, stirbt mit ihm. Ein Socket oder eine beobachtete Datei waere
    adressierbar — also eine Tuer.
*/

#ifndef AIZANA_UITEST_HOOK
 #define AIZANA_UITEST_HOOK 0
#endif

namespace aizana::uitest
{

#if AIZANA_UITEST_HOOK

/**
 * Liest Kommandos von stdin und setzt sie in synthetische Mausereignisse um.
 *
 * Scharf nur, wenn BEIDE Bedingungen gelten:
 *   - Umgebungsvariable AIZANA_UITEST=1
 *   - stdin ist eine Pipe (S_ISFIFO) — nicht Terminal, nicht Datei
 */
class Hook
{
public:
    ~Hook();

    /** Prueft die Bedingungen und startet den Leser-Thread. nullptr = nicht scharf. */
    static std::unique_ptr<Hook> armIfRequested();

private:
    Hook();

    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Hook)
};

/**
 * True, sobald der Hook scharf ist.
 *
 * Der Testmodus VERKLEINERT die App, er vergroessert sie nie: alles, was aus dem
 * Prozess hinausreicht, wird abgeschaltet (Loopback-Server der KI-Bruecke,
 * CLI-Start des KI-Studios, Modellgewichte-Download). Ein blind gesetzter Klick
 * soll keine Tuer aufstossen koennen, die der Hook selbst gar nicht hat.
 */
bool isArmed() noexcept;

#else

/** Ausgelieferter Build: existiert nicht. Der Compiler faltet jede Abfrage weg. */
constexpr bool isArmed() noexcept { return false; }

#endif

} // namespace aizana::uitest
