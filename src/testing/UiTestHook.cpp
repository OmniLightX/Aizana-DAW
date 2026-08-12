#include "UiTestHook.h"

#if AIZANA_UITEST_HOOK

#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

/*
    VERBOTENE WERKZEUGE IN DIESER UEBERSETZUNGSEINHEIT (geprueft von
    scripts/uitest-guard-check.sh an der Symboltabelle, nicht per Zusage):

      CGEvent* / CGWarpMouseCursorPosition  — systemweite Eingaben bzw. Zeiger
      AXUIElement* / NSAppleScript          — Fernsteuerung fremder Prozesse
      juce::ChildProcess                    — neuer Prozess = Ausgang
      juce::StreamingSocket / DatagramSocket — adressierbare Tuer
      juce::URL / WebInputStream            — Netz
      performExternalDragDrop*              — verlaesst den Prozess Richtung Finder

    Was hier eingespeist wird, betritt den Bildschirm nie. Es gibt in
    ComponentPeer::handleMouseEvent keinen Parameter, der ein fremdes Fenster
    benennen koennte.
*/

namespace aizana::uitest
{

namespace
{

std::atomic<bool> armedFlag { false };

//==============================================================================
// Antwort-Protokoll: beliebig viele Datenzeilen "| ...", danach GENAU eine
// Abschlusszeile, die mit OK oder ERR beginnt. Der Harness liest bis dorthin.
//==============================================================================

void writeLine (const juce::String& s)
{
    const auto utf8 = s.toRawUTF8();
    std::fwrite (utf8, 1, std::strlen (utf8), stdout);
    std::fputc ('\n', stdout);
    std::fflush (stdout);
}

//==============================================================================

bool parseMods (const juce::String& spec, juce::ModifierKeys& out, juce::String& err)
{
    int flags = 0;

    if (spec.isNotEmpty() && spec != "-")
    {
        for (auto raw : juce::StringArray::fromTokens (spec, ",+", ""))
        {
            const auto tok = raw.trim().toLowerCase();

            if      (tok.isEmpty())  continue;
            else if (tok == "shift") flags |= juce::ModifierKeys::shiftModifier;
            else if (tok == "ctrl")  flags |= juce::ModifierKeys::ctrlModifier;
            else if (tok == "alt")   flags |= juce::ModifierKeys::altModifier;
            else if (tok == "cmd")   flags |= juce::ModifierKeys::commandModifier;
            else { err = "ERR unbekannter Modifier: " + tok; return false; }
        }
    }

    // EINZIGER bekannter Pfad, auf dem reine JUCE-Mausbehandlung die SYSTEMMAUS
    // anfasst: mit ctrl+alt+cmd zusammen kippt Slider::isAbsoluteDragMode
    // (juce_Slider.cpp:1183) in den Velocity-Modus, der
    // enableUnboundedMouseMovement(true) setzt (juce_Slider.cpp:848); beim
    // Loslassen holt JUCE den Zeiger per setScreenPosition in die Component
    // zurueck (juce_MouseInputSourceImpl.h:464) -> MouseInputSource::
    // setRawMousePosition -> CGWarpMouseCursorPosition (juce_Windowing_mac.mm:275).
    // Der ECHTE Mauszeiger des Users wuerde springen. Deshalb hart abgelehnt;
    // cmd allein (Marquee) bleibt erlaubt, testFlags verlangt alle drei.
    if ((flags & juce::ModifierKeys::ctrlAltCommandModifiers) == juce::ModifierKeys::ctrlAltCommandModifiers)
    {
        err = "ERR verboten: ctrl+alt+cmd gemeinsam schaltet JUCE-Slider in den Velocity-Modus "
              "und bewegt beim Loslassen den ECHTEN Mauszeiger (CGWarpMouseCursorPosition)";
        return false;
    }

    out = juce::ModifierKeys (flags);
    return true;
}

juce::ComponentPeer* peerAt (int index)
{
    return juce::isPositiveAndBelow (index, juce::ComponentPeer::getNumPeers())
             ? juce::ComponentPeer::getPeer (index)
             : nullptr;
}

int indexOfPeer (const juce::ComponentPeer* peer)
{
    for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        if (juce::ComponentPeer::getPeer (i) == peer)
            return i;

    return -1;
}

juce::String describe (const juce::Component& c)
{
    auto label = c.getTitle();

    if (label.isEmpty())
        label = c.getName();

    if (label.isEmpty())
        if (auto* b = dynamic_cast<const juce::Button*> (&c))
            label = b->getButtonText();

    return label;
}

void collectMatches (juce::Component& c, const juce::String& name, juce::Array<juce::Component*>& out)
{
    if (c.isShowing() && describe (c) == name)
        out.add (&c);

    for (auto* child : c.getChildren())
        if (child != nullptr)
            collectMatches (*child, name, out);
}

void dumpTree (const juce::Component& c, int depth, int maxDepth, int& budget, juce::StringArray& out)
{
    if (budget <= 0 || depth > maxDepth)
        return;

    const auto label = describe (c);

    if (c.isShowing() && label.isNotEmpty())
    {
        --budget;
        out.add ("| " + juce::String::repeatedString ("  ", depth) + label
                   + "  [" + juce::String (typeid (c).name()).substring (0, 40) + "]"
                   + "  " + c.getScreenBounds().toString());
    }

    for (auto* child : c.getChildren())
        if (child != nullptr)
            dumpTree (*child, depth + (label.isNotEmpty() ? 1 : 0), maxDepth, budget, out);
}

//==============================================================================
// Alles hier laeuft auf dem MESSAGE THREAD (I-02/I-08: TE- und UI-Zustand wird
// ausschliesslich dort angefasst).
//==============================================================================

juce::String sendMouse (int peerIndex, float x, float y, juce::ModifierKeys mods)
{
    auto* peer = peerAt (peerIndex);

    if (peer == nullptr)
        return "ERR kein Fenster mit Index " + juce::String (peerIndex);

    peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse,
                            { x, y },
                            mods,
                            juce::MouseInputSource::defaultPressure,
                            juce::MouseInputSource::defaultOrientation,
                            juce::Time::currentTimeMillis());

    // Stolperdraht auf genau die Bedingung, die zum Zeiger-Warp fuehren wuerde.
    // Wenn das je anspricht, ist eine Annahme ueber JUCE gekippt — dann lieber
    // laut abbrechen als dem User still den Mauszeiger wegziehen.
    if (juce::Desktop::getInstance().getMainMouseSource().isUnboundedMouseMovementEnabled())
        return "ERR unbounded-mouse-mode aktiv — Geste abgebrochen, sonst springt der echte Zeiger";

    return "OK";
}

juce::String cmdPeers()
{
    juce::StringArray lines;

    for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
    {
        if (auto* peer = juce::ComponentPeer::getPeer (i))
        {
            auto& c = peer->getComponent();
            const auto b = c.getScreenBounds();
            lines.add ("| peer=" + juce::String (i)
                         + " sx=" + juce::String (b.getX())
                         + " sy=" + juce::String (b.getY())
                         + " sw=" + juce::String (b.getWidth())
                         + " sh=" + juce::String (b.getHeight())
                         + " focused=" + (peer->isFocused() ? "1" : "0")
                         + " label=\"" + describe (c) + "\"");
        }
    }

    lines.add ("OK " + juce::String (juce::ComponentPeer::getNumPeers()));
    return lines.joinIntoString ("\n");
}

juce::String cmdTree (int peerIndex, int maxDepth)
{
    auto* peer = peerAt (peerIndex);

    if (peer == nullptr)
        return "ERR kein Fenster mit Index " + juce::String (peerIndex);

    juce::StringArray lines;
    int budget = 600;
    dumpTree (peer->getComponent(), 0, maxDepth, budget, lines);
    lines.add (budget <= 0 ? "OK gekuerzt" : "OK " + juce::String (lines.size()));
    return lines.joinIntoString ("\n");
}

juce::String cmdFind (const juce::String& name)
{
    juce::Array<juce::Component*> hits;

    for (int i = 0; i < juce::ComponentPeer::getNumPeers(); ++i)
        if (auto* peer = juce::ComponentPeer::getPeer (i))
            collectMatches (peer->getComponent(), name, hits);

    if (hits.isEmpty())
        return "ERR nicht gefunden: " + name;

    // NIE still den ersten Treffer nehmen: ein mehrdeutiger Name ist genau die
    // Fehlerklasse, gegen die die Benennungswelle gebaut wurde (Koordinaten- bzw.
    // Namensklicks, die ins Leere gehen und trotzdem Erfolg melden).
    if (hits.size() > 1)
    {
        juce::StringArray lines;

        for (auto* h : hits)
            lines.add ("| " + h->getScreenBounds().toString());

        lines.add ("ERR mehrdeutig: " + juce::String (hits.size()) + "x \"" + name + "\"");
        return lines.joinIntoString ("\n");
    }

    auto* comp = hits.getFirst();
    auto* peer = comp->getPeer();

    if (peer == nullptr)
        return "ERR \"" + name + "\" haengt an keinem Fenster";

    // Nur den SICHTBAREN Ausschnitt melden. Scrollende Flaechen sind viel groesser
    // als das Fenster — die Clip-Flaeche einer Spur ist 30000 px breit. Ein Anteil
    // von 40 % laege dann weit ausserhalb des Bildschirms, und der Klick ginge ins
    // Leere, ohne dass es auffiele. Geschnitten wird gegen das Fenster.
    const auto screen = comp->getScreenBounds().toFloat()
                            .getIntersection (peer->getComponent().getScreenBounds().toFloat());

    if (screen.isEmpty())
        return "ERR \"" + name + "\" liegt ausserhalb des sichtbaren Fensters";

    const auto tl     = peer->globalToLocal (screen.getTopLeft());
    const auto centre = peer->globalToLocal (screen.getCentre());

    return "OK peer=" + juce::String (indexOfPeer (peer))
             + " x=" + juce::String (centre.x, 1)
             + " y=" + juce::String (centre.y, 1)
             + " left=" + juce::String (tl.x, 1)
             + " top=" + juce::String (tl.y, 1)
             + " w=" + juce::String (screen.getWidth(), 1)
             + " h=" + juce::String (screen.getHeight(), 1);
}

juce::String cmdDrop (int peerIndex, int x, int y, const juce::String& paths)
{
    auto* peer = peerAt (peerIndex);

    if (peer == nullptr)
        return "ERR kein Fenster mit Index " + juce::String (peerIndex);

    juce::ComponentPeer::DragInfo info;
    info.files    = juce::StringArray::fromTokens (paths, "|", "");
    info.position = { x, y };

    for (const auto& f : info.files)
        if (! juce::File (f).existsAsFile() && ! juce::File (f).isDirectory())
            return "ERR Pfad existiert nicht: " + f;

    peer->handleDragMove (info);
    const bool taken = peer->handleDragDrop (info);
    return taken ? "OK" : "ERR Ziel hat den Drop nicht angenommen";
}

juce::String cmdKey (int peerIndex, const juce::String& keyName, juce::ModifierKeys mods)
{
    auto* peer = peerAt (peerIndex);

    if (peer == nullptr)
        return "ERR kein Fenster mit Index " + juce::String (peerIndex);

    int code = 0;
    juce::juce_wchar text = 0;
    const auto k = keyName.toLowerCase();

    if      (k == "return" || k == "enter") code = juce::KeyPress::returnKey;
    else if (k == "escape" || k == "esc")   code = juce::KeyPress::escapeKey;
    else if (k == "tab")                    code = juce::KeyPress::tabKey;
    else if (k == "backspace")              code = juce::KeyPress::backspaceKey;
    else if (k == "delete")                 code = juce::KeyPress::deleteKey;
    else if (k == "left")                   code = juce::KeyPress::leftKey;
    else if (k == "right")                  code = juce::KeyPress::rightKey;
    else if (k == "up")                     code = juce::KeyPress::upKey;
    else if (k == "down")                   code = juce::KeyPress::downKey;
    else if (k == "space")                  { code = juce::KeyPress::spaceKey; text = ' '; }
    else if (keyName.length() == 1)         { code = (int) keyName[0]; text = keyName[0]; }
    else return "ERR unbekannte Taste: " + keyName;

    return peer->handleKeyPress (juce::KeyPress (code, mods, text)) ? "OK" : "ERR Taste nicht angenommen";
}

juce::String cmdText (int peerIndex, const juce::String& text)
{
    auto* peer = peerAt (peerIndex);

    if (peer == nullptr)
        return "ERR kein Fenster mit Index " + juce::String (peerIndex);

    for (auto ch : text)
        peer->handleKeyPress (juce::KeyPress ((int) ch, juce::ModifierKeys(), ch));

    return "OK " + juce::String (text.length());
}

juce::String cmdMenu (const juce::StringArray& args)
{
   #if JUCE_MAC
    auto* model = juce::MenuBarModel::getMacMainMenu();
   #else
    juce::MenuBarModel* model = nullptr;
   #endif

    if (model == nullptr)
        return "ERR keine Menueleiste";

    // Die Menueleiste ist auf macOS eine NATIVE NSMenu (MainWindow.cpp:30) und
    // damit fuer handleMouseEvent grundsaetzlich unerreichbar. Statt deshalb doch
    // wieder zu systemweiter Maussteuerung zu greifen, wird der Menuepunkt hier
    // direkt am Modell ausgeloest — ein gewoehnlicher Methodenaufruf im Prozess.
    if (args.size() <= 1)
    {
        juce::StringArray lines;
        const auto names = model->getMenuBarNames();

        for (int i = 0; i < names.size(); ++i)
        {
            lines.add ("| [" + juce::String (i) + "] " + names[i]);
            juce::String dummy;
            auto menu = model->getMenuForIndex (i, names[i]);

            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (const auto& item = it.getItem(); item.itemID != 0)
                    lines.add ("|     " + juce::String (item.itemID) + "  " + item.text);
        }

        lines.add ("OK " + juce::String (names.size()));
        return lines.joinIntoString ("\n");
    }

    if (args.size() < 3)
        return "ERR menu <topIndex> <itemId>";

    model->menuItemSelected (args[2].getIntValue(), args[1].getIntValue());
    return "OK";
}

} // anonymous namespace

//==============================================================================

struct Hook::Impl : public juce::Thread
{
    Impl() : juce::Thread ("aizana-uitest") { startThread(); }

    ~Impl() override
    {
        signalThreadShouldExit();
        stopThread (2000);
    }

    void run() override
    {
        writeLine ("| Aizana UI-Test-Hook scharf (nur dieser Prozess, keine Systemeingaben)");
        writeLine ("OK bereit");

        juce::String pending;
        char buf[4096];

        while (! threadShouldExit())
        {
            pollfd p { STDIN_FILENO, POLLIN, 0 };
            const int r = ::poll (&p, 1, 120);

            if (r < 0)
            {
                if (errno == EINTR)
                    continue;

                break;
            }

            if (r == 0)
                continue;

            const auto n = ::read (STDIN_FILENO, buf, sizeof (buf));

            if (n <= 0)
                break;                          // EOF: der startende Prozess ist weg

            pending += juce::String::fromUTF8 (buf, (int) n);

            for (;;)
            {
                const auto nl = pending.indexOfChar ('\n');

                if (nl < 0)
                    break;

                const auto line = pending.substring (0, nl).trim();
                pending = pending.substring (nl + 1);

                if (line.isNotEmpty() && ! line.startsWith ("#"))
                    writeLine (handleLine (line));
            }
        }
    }

    /** Marshalling auf den Message-Thread; der shared_ptr ueberlebt einen Timeout. */
    static juce::String onMessageThread (std::function<juce::String()> fn, int timeoutMs = 20000)
    {
        struct Shared
        {
            juce::WaitableEvent done;
            juce::String reply;
        };

        auto shared = std::make_shared<Shared>();

        juce::MessageManager::callAsync ([shared, fn = std::move (fn)]
        {
            shared->reply = fn();
            shared->done.signal();
        });

        if (! shared->done.wait (timeoutMs))
            return "ERR Zeitueberschreitung — Message-Thread blockiert "
                   "(offener nativer Dialog? Die kann der Hook nicht bedienen, siehe Bericht)";

        return shared->reply;
    }

    /** Zusammengesetzte Gesten: mehrere Einzelereignisse mit Pausen dazwischen. */
    juce::String gesture (const std::vector<std::function<juce::String()>>& steps, int gapMs)
    {
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const auto r = onMessageThread (steps[i]);

            if (r.startsWith ("ERR"))
                return r;

            if (i + 1 < steps.size())
                wait (gapMs);
        }

        return "OK";
    }

    juce::String handleLine (const juce::String& line)
    {
        const auto args = juce::StringArray::fromTokens (line, " \t", "\"");
        const auto cmd  = args[0].toLowerCase();

        const auto num = [&args] (int i, float dflt = 0.0f)
        {
            return juce::isPositiveAndBelow (i, args.size()) ? args[i].getFloatValue() : dflt;
        };

        const auto tail = [&line] (int skipTokens)
        {
            auto s = line;

            for (int i = 0; i < skipTokens; ++i)
                s = s.fromFirstOccurrenceOf (" ", false, false).trimStart();

            return s.trim();
        };

        //----------------------------------------------------------------------
        if (cmd == "hilfe" || cmd == "help")
            return "| peers | tree <peer> [tiefe] | find <name> | front <peer>\n"
                   "| move/down/up <peer> <x> <y> [mods] | click|dblclick <peer> <x> <y> [mods]\n"
                   "| drag <peer> <x1> <y1> <x2> <y2> [schritte] [mods]\n"
                   "| drop <peer> <x> <y> <pfad[|pfad]> | key <peer> <taste> [mods] | text <peer> <text>\n"
                   "| menu [top item] | warte <ms> | quit\n"
                   "OK";

        if (cmd == "peers")
            return onMessageThread ([] { return cmdPeers(); });

        if (cmd == "tree")
            return onMessageThread ([&args, num] { return cmdTree ((int) num (1), args.size() > 2 ? (int) num (2) : 12); });

        if (cmd == "find")
        {
            const auto name = tail (1);
            return name.isEmpty() ? juce::String ("ERR find <name>")
                                  : onMessageThread ([name] { return cmdFind (name); });
        }

        if (cmd == "front")
        {
            const int idx = (int) num (1);
            return onMessageThread ([idx]() -> juce::String
            {
                if (auto* peer = peerAt (idx))
                {
                    peer->toFront (true);
                    return "OK";
                }

                return "ERR kein Fenster mit Index " + juce::String (idx);
            });
        }

        if (cmd == "warte" || cmd == "wait")
        {
            wait (juce::jlimit (1, 10000, (int) num (1, 200)));
            return "OK";
        }

        if (cmd == "quit")
        {
            onMessageThread ([]() -> juce::String
            {
                if (auto* app = juce::JUCEApplication::getInstance())
                    app->systemRequestedQuit();

                return "OK";
            });

            signalThreadShouldExit();
            return "OK beendet";
        }

        if (cmd == "menu")
            return onMessageThread ([args] { return cmdMenu (args); });

        if (cmd == "drop")
        {
            const int  peer  = (int) num (1);
            const int  x     = (int) num (2);
            const int  y     = (int) num (3);
            const auto paths = tail (4);
            return onMessageThread ([peer, x, y, paths] { return cmdDrop (peer, x, y, paths); });
        }

        if (cmd == "key")
        {
            juce::ModifierKeys mods;
            juce::String err;

            if (! parseMods (args.size() > 3 ? args[3] : juce::String(), mods, err))
                return err;

            const int  peer = (int) num (1);
            const auto name = args[2];
            return onMessageThread ([peer, name, mods] { return cmdKey (peer, name, mods); });
        }

        if (cmd == "text")
        {
            const int  peer = (int) num (1);
            const auto txt  = tail (2);
            return onMessageThread ([peer, txt] { return cmdText (peer, txt); });
        }

        //---------------------------------------------------------------- Maus
        static const juce::StringArray mouseCmds { "move", "moveb", "down", "up", "click", "dblclick", "drag" };

        if (! mouseCmds.contains (cmd))
            return "ERR unbekanntes Kommando: " + cmd + " (siehe hilfe)";

        const int peer = (int) num (1);
        juce::ModifierKeys mods;
        juce::String err;

        const int modArg = (cmd == "drag") ? 7 : 5;

        if (! parseMods (args.size() > modArg ? args[modArg] : juce::String(), mods, err))
            return err;

        const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
        const float x = num (2), y = num (3);

        if (cmd == "move")   return onMessageThread ([=] { return sendMouse (peer, x, y, mods); });
        if (cmd == "moveb")  return onMessageThread ([=] { return sendMouse (peer, x, y, held); });
        if (cmd == "down")   return onMessageThread ([=] { return sendMouse (peer, x, y, held); });
        if (cmd == "up")     return onMessageThread ([=] { return sendMouse (peer, x, y, mods); });

        if (cmd == "click" || cmd == "dblclick")
        {
            std::vector<std::function<juce::String()>> steps {
                [=] { return sendMouse (peer, x, y, mods); },
                [=] { return sendMouse (peer, x, y, held); },
                [=] { return sendMouse (peer, x, y, mods); }
            };

            if (cmd == "dblclick")
            {
                steps.push_back ([=] { return sendMouse (peer, x, y, held); });
                steps.push_back ([=] { return sendMouse (peer, x, y, mods); });
            }

            return gesture (steps, 40);
        }

        // drag <peer> <x1> <y1> <x2> <y2> [schritte] [mods]
        const float x2 = num (4), y2 = num (5);
        const int steps = juce::jlimit (2, 200, args.size() > 6 ? (int) num (6) : 24);

        std::vector<std::function<juce::String()>> seq;
        seq.push_back ([=] { return sendMouse (peer, x, y, mods); });     // hover
        seq.push_back ([=] { return sendMouse (peer, x, y, held); });     // press

        for (int i = 1; i <= steps; ++i)
        {
            const float t  = (float) i / (float) steps;
            const float px = x + (x2 - x) * t;
            const float py = y + (y2 - y) * t;
            seq.push_back ([=] { return sendMouse (peer, px, py, held); });
        }

        seq.push_back ([=] { return sendMouse (peer, x2, y2, mods); });   // release

        return gesture (seq, 12);
    }
};

//==============================================================================

Hook::Hook() : impl (std::make_unique<Impl>()) {}
Hook::~Hook() = default;

bool isArmed() noexcept { return armedFlag.load(); }

std::unique_ptr<Hook> Hook::armIfRequested()
{
    // Riegel 2a — Umgebungsvariable.
    if (juce::SystemStats::getEnvironmentVariable ("AIZANA_UITEST", {}) != "1")
        return nullptr;

    // Riegel 2b — stdin MUSS eine unbenannte Pipe sein. Kein Terminal (dann sitzt
    // ein Mensch davor), keine Datei (die haette einen Pfad und waere damit fuer
    // jeden anderen Prozess adressierbar).
    struct stat st {};

    if (::fstat (STDIN_FILENO, &st) != 0 || ! S_ISFIFO (st.st_mode))
    {
        std::fprintf (stderr, "[uitest] AIZANA_UITEST=1 gesetzt, aber stdin ist keine Pipe — Hook bleibt aus.\n");
        std::fflush (stderr);
        return nullptr;
    }

    armedFlag.store (true);
    return std::unique_ptr<Hook> (new Hook());
}

} // namespace aizana::uitest

#endif // AIZANA_UITEST_HOOK
