#!/usr/bin/env python3
"""uitest.py — Treiber fuer den In-App-Maus-Hook (Ziehbewegungen in GUI-Testlaeufen).

Die Steuerleitung ist eine UNBENANNTE PIPE: subprocess.Popen(stdin=PIPE, stdout=PIPE)
legt genau das an — kein Pfad, kein Port, kein Eintrag in irgendeinem Namensraum.
Nur dieser Prozess kann hineinschreiben, und sie stirbt mit ihm. Ein Socket oder eine
beobachtete Datei waere fuer jeden anderen Prozess adressierbar, also eine Tuer.

(Frueher als bash-Coprozess gebaut — /bin/bash auf macOS ist 3.2 und kennt `coproc`
nicht. Python gibt dieselbe Eigenschaft ohne Klimmzuege.)

    ./scripts/uitest.py lauf drehbuch.txt
    ./scripts/uitest.py lauf -          # Drehbuch von stdin

Drehbuch-Befehle siehe docs/UITEST_HOOK.md.
"""
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = Path(os.environ.get(
    "AIZANA_UITEST_APP",
    ROOT / "build-uitest/Aizana_artefacts/Release/Aizana.app/Contents/MacOS/Aizana"))
SHOTS = Path(os.environ.get("AIZANA_SHOTS", "/tmp/aizana_gui"))
LOG = Path(os.environ.get("AIZANA_UITEST_LOG", "/tmp/aizana_uitest.log"))


class Hook:
    def __init__(self, app: Path, logfile):
        self.log = logfile
        self.proc = subprocess.Popen(
            [str(app)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=logfile,
            env={**os.environ, "AIZANA_UITEST": "1"},
            text=True, bufsize=1)

    def send(self, line, timeout=30):
        """Ein Kommando, eine Antwort. Rueckgabe: (erfolg, zeilen)."""
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        return self.read(timeout)

    def read(self, timeout=30):
        deadline = time.time() + timeout
        daten = []
        while time.time() < deadline:
            if self.proc.poll() is not None:
                return False, ["ERR die App ist beendet"]
            zeile = self.proc.stdout.readline()
            if zeile == "":
                return False, ["ERR Verbindung zu"]
            zeile = zeile.rstrip("\n")
            if zeile.startswith("| "):
                daten.append(zeile[2:])
            elif zeile.startswith("OK") or zeile.startswith("ERR"):
                daten.append(zeile)
                return not zeile.startswith("ERR"), daten
            else:
                self.log.write(zeile + "\n")   # Engine-Rauschen
        return False, ["ERR Zeitueberschreitung"]

    letzter_fehler = ""

    def finde(self, name):
        """-> dict mit peer/x/y/left/top/w/h  oder None."""
        ok, zeilen = self.send("find " + name)
        if not ok:
            # Den ECHTEN Grund weiterreichen: "nicht gefunden" und "mehrdeutig" sind
            # verschiedene Fehler und brauchen verschiedene Reaktionen.
            self.letzter_fehler = zeilen[-1] if zeilen else "ERR ohne Angabe"
            return None
        letzte = zeilen[-1]
        werte = {}
        for teil in letzte[len("OK "):].split():
            if "=" in teil:
                k, v = teil.split("=", 1)
                werte[k] = float(v)
        return werte if "peer" in werte else None

    def fensterrahmen(self):
        """Bildschirmrechteck des Hauptfensters -> (x, y, w, h) oder None."""
        ok, zeilen = self.send("peers", timeout=10)
        if not ok:
            return None
        for z in zeilen:
            if z.startswith("peer=0 "):
                w = {k: v for k, v in (t.split("=", 1) for t in z.split() if "=" in t)}
                try:
                    return int(w["sx"]), int(w["sy"]), int(w["sw"]), int(w["sh"])
                except (KeyError, ValueError):
                    return None
        return None

    def beenden(self):
        try:
            self.send("quit", timeout=8)
        except Exception:
            pass
        try:
            self.proc.wait(timeout=8)
        except Exception:
            self.proc.kill()


def vorn():
    subprocess.run(["osascript", "-e",
                    'tell application "System Events" to set frontmost of process "Aizana" to true'],
                   capture_output=True)


def zeige(zeilen, einzug="  "):
    for z in zeilen:
        print(einzug + z)


def main():
    if len(sys.argv) < 3 or sys.argv[1] != "lauf":
        print(__doc__)
        return 2

    if not APP.exists():
        print(f"FEHLT: {APP}\n  cmake -S . -B build-uitest -DCMAKE_BUILD_TYPE=Release "
              f"-DAIZANA_UITEST_HOOK=ON && cmake --build build-uitest --target Aizana -j 10")
        return 1

    # JUCE laesst nur EINE Instanz zu (Main.cpp: moreThanOneInstanceAllowed). Laeuft schon
    # eine, reicht unser Start nur an sie weiter und beendet sich — der Hook waere still
    # tot und jeder Befehl liefe ins Leere, ohne dass es auffiele.
    if subprocess.run(["pgrep", "-x", "Aizana"], capture_output=True).returncode == 0:
        print("ABBRUCH: es laeuft bereits eine Aizana-Instanz. Erst beenden.")
        return 1

    quelle = sys.stdin if sys.argv[2] == "-" else open(sys.argv[2], encoding="utf-8")
    SHOTS.mkdir(parents=True, exist_ok=True)

    with open(LOG, "w", encoding="utf-8") as logfile:
        hook = Hook(APP, logfile)
        print(f"== Aizana startet (Hook scharf, PID {hook.proc.pid}) ==")
        ok, zeilen = hook.read(timeout=25)
        zeige(zeilen)
        if not ok:
            hook.beenden()
            return 1

        # Auf das Hauptfenster warten.
        for _ in range(40):
            ok, zeilen = hook.send("peers", timeout=10)
            if ok and not zeilen[-1].startswith("OK 0"):
                break
            time.sleep(1)
        print("== Fenster da ==")

        fehler = 0
        for roh in quelle:
            # Umgebungsvariablen aufloesen, damit Drehbuecher keine Rechner-Pfade
            # festschreiben ($AIZANA_TESTWAV statt /tmp/... oder /Users/...).
            zeile = os.path.expandvars(roh.strip())
            if not zeile or zeile.startswith("#"):
                continue
            teile = zeile.split(None, 1)
            befehl = teile[0]
            rest = teile[1] if len(teile) > 1 else ""
            print("-> " + zeile)

            if befehl == "bild":
                # Die App zeichnet ihr eigenes Fenster. Unabhaengig von Vordergrund,
                # Space und Bildschirmaufnahme-Berechtigung — und sie kann nur sich
                # selbst abbilden, nie fremde Fenster.
                ziel = SHOTS / (rest + ".png")
                ok, zeilen = hook.send(f"bild 0 {ziel}", 40)
                if ok and ziel.exists():
                    klein = SHOTS / (rest + "_klein.png")
                    subprocess.run(["sips", "-Z", "1400", str(ziel), "--out", str(klein)],
                                   capture_output=True)
                    print(f"   {klein}   ({zeilen[-1]})")
                else:
                    print("   " + (zeilen[-1] if zeilen else "ERR kein Bild")); fehler += 1
                continue

            if befehl == "bildschirm":
                vorn(); time.sleep(0.4)
                ziel = SHOTS / (rest + ".png")
                # Nur das Fenster ablichten, nicht den ganzen Bildschirm: sonst steht
                # auf jedem Beleg, was zufaellig sonst noch offen war.
                # screencapture -R schlaegt auf diesem Rechner fehl ("could not create
                # image from rect", auch bei 0,0,200,200), Vollbild geht. Also Vollbild
                # aufnehmen und danach auf das Fenster zuschneiden. Retina: das Bild ist
                # doppelt so gross wie die Punkt-Koordinaten, daher der Faktor.
                rahmen = hook.fensterrahmen()
                if subprocess.run(["screencapture", "-x", str(ziel)],
                                  capture_output=True).returncode != 0 or not ziel.exists():
                    print("   ERR Screenshot fehlgeschlagen"); fehler += 1; continue

                if rahmen:
                    breite = int(subprocess.run(
                        ["sips", "-g", "pixelWidth", str(ziel)],
                        capture_output=True, text=True).stdout.split()[-1])
                    # Punkt-Breite des Hauptschirms kennen wir nicht direkt; der Faktor
                    # ergibt sich aus Bildbreite / logischer Schirmbreite. Die logische
                    # Breite ist mindestens Fensterrand rechts — konservativ ueber die
                    # gaengigen Faktoren 1 und 2 entscheiden.
                    f = 2 if breite >= 2 * (rahmen[0] + rahmen[2]) else 1
                    subprocess.run(["magick", str(ziel), "-crop",
                                    f"{rahmen[2]*f}x{rahmen[3]*f}+{rahmen[0]*f}+{rahmen[1]*f}",
                                    "+repage", str(ziel)], capture_output=True)

                klein = SHOTS / (rest + "_klein.png")
                subprocess.run(["sips", "-Z", "1400", str(ziel), "--out", str(klein)],
                               capture_output=True)
                # Erfolg NUR melden, wenn die Datei wirklich da ist — vorher hat dieser
                # Befehl einen Pfad ausgegeben, ohne ihn je zu pruefen.
                if klein.exists():
                    print(f"   {klein}")
                else:
                    print("   ERR Screenshot nicht entstanden"); fehler += 1
                continue

            if befehl == "vorn":
                # Erst IN der App (macht aus dem direkt gestarteten Prozess eine
                # regulaere Vordergrund-App), dann ueber die Bedienungshilfen.
                hook.send("front 0", 10)
                vorn()
                # WARTEN, bis das Fenster wirklich vorn ist. Das Nach-vorn-Holen
                # laeuft asynchron; ein Klick mitten in die Umschaltphase kommt an,
                # bewirkt aber nichts — gemessen am Wechsel nach "Pro", der ohne
                # dieses Warten zuverlaessig verpuffte und trotzdem OK meldete.
                for _ in range(20):
                    ok, zeilen = hook.send("peers", timeout=10)
                    if ok and any("focused=1" in z for z in zeilen):
                        break
                    time.sleep(0.25)
                else:
                    print("   (Hinweis: Fenster meldet keinen Fokus)")
                continue

            # wennfehlt <Pruefname> @ <Knopfname>
            # Umschalter blind zu druecken ist unzuverlaessig, sobald die App ihren
            # Zustand merkt (Dock-Editor, Ansichtsmodus): derselbe Klick oeffnet mal
            # und schliesst mal. Deshalb erst pruefen, ob das Ziel schon da ist.
            if befehl == "wennfehlt":
                pruef, _, knopf = rest.partition("@")
                if hook.finde(pruef.strip()) is not None:
                    print("   OK schon da"); continue
                ziel = hook.finde(knopf.strip())
                if ziel is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                ok, zeilen = hook.send(f"click {int(ziel['peer'])} {ziel['x']} {ziel['y']}", 40)
                zeige(zeilen, "   ")
                if not ok:
                    fehler += 1
                    continue

                # NACHPRUEFEN: ein Klick, der nichts bewirkt, meldet trotzdem OK.
                # Ohne diese Schleife laeuft das ganze Drehbuch im falschen Zustand
                # weiter und jeder Folgeschritt scheitert aus scheinbar anderem Grund.
                for _ in range(12):
                    time.sleep(0.5)
                    if hook.finde(pruef.strip()) is not None:
                        print("   OK Zustand hergestellt")
                        break
                else:
                    print(f"   ERR nach dem Klick auf \"{knopf.strip()}\" fehlt weiterhin "
                          f"\"{pruef.strip()}\"")
                    fehler += 1
                continue

            if befehl in ("klick", "dklick", "rklick"):
                ziel = hook.finde(rest)
                if ziel is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                k = {"klick": "click", "dklick": "dblclick", "rklick": "rclick"}[befehl]
                ok, zeilen = hook.send(f"{k} {int(ziel['peer'])} {ziel['x']} {ziel['y']}", 40)

            elif befehl == "ziehe":
                *namensteile, dx, dy = rest.split()
                ziel = hook.finde(" ".join(namensteile))
                if ziel is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                ok, zeilen = hook.send(
                    f"drag {int(ziel['peer'])} {ziel['x']} {ziel['y']} "
                    f"{ziel['x'] + float(dx)} {ziel['y'] + float(dy)} 24", 60)

            elif befehl == "ziehevon":
                a, _, b = rest.partition(" nach ")
                za, zb = hook.finde(a.strip()), hook.finde(b.strip())
                if za is None or zb is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                ok, zeilen = hook.send(
                    f"drag {int(za['peer'])} {za['x']} {za['y']} {zb['x']} {zb['y']} 24", 60)

            elif befehl == "drop":
                name, _, pfad = rest.partition(" ")
                ziel = hook.finde(name)
                if ziel is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                ok, zeilen = hook.send(
                    f"drop {int(ziel['peer'])} {int(ziel['x'])} {int(ziel['y'])} {pfad}", 40)

            # Punkt INNERHALB eines benannten Bereichs, angegeben als Anteil 0..1.
            # So bleibt das Ziel benannt (ueberlebt Layout-Aenderungen), und trotzdem
            # laesst sich eine Stelle darin treffen — z.B. Sekunde X einer Spur.
            # Schreibweise:  <befehl> <Name> @ <zahlen...> [pfad]
            # Das @ trennt den Namen von den Zahlen — Namen UND Dateipfade duerfen
            # Leerzeichen enthalten ("Spurbereich Track 1", "121 bpm_809.wav").
            elif befehl in ("klickin", "dklickin", "rklickin", "ziehein", "dropin"):
                if "@" not in rest:
                    print("   ERR Schreibweise: " + befehl + " <Name> @ <zahlen> [pfad]")
                    fehler += 1; continue
                name, _, hinten = rest.partition("@")
                name = name.strip()
                if befehl == "dropin":
                    stuecke = hinten.split(None, 2)      # 2 Anteile + Pfad (darf Leerzeichen haben)
                    zahlen = stuecke[:2]; pfad = stuecke[2] if len(stuecke) > 2 else ""
                else:
                    zahlen = hinten.split(); pfad = ""
                ziel = hook.finde(name)
                if ziel is None:
                    print("   " + hook.letzter_fehler); fehler += 1; continue
                px = lambda fx: ziel["left"] + float(fx) * ziel["w"]
                py = lambda fy: ziel["top"] + float(fy) * ziel["h"]
                p = int(ziel["peer"])
                if befehl == "ziehein":
                    # Optionaler fuenfter Wert = Modifier (cmd = Marquee-Zeitbereich).
                    # drag erwartet ihn HINTER der Schrittzahl (Index 7 im Rohbefehl).
                    zmods = zahlen[4] if len(zahlen) > 4 else "-"
                    ok, zeilen = hook.send(
                        f"drag {p} {px(zahlen[0])} {py(zahlen[1])} "
                        f"{px(zahlen[2])} {py(zahlen[3])} 24 {zmods}", 60)
                elif befehl == "dropin":
                    ok, zeilen = hook.send(
                        f"drop {p} {int(px(zahlen[0]))} {int(py(zahlen[1]))} {pfad}", 40)
                else:
                    k = {"klickin": "click", "dklickin": "dblclick",
                         "rklickin": "rclick"}[befehl]
                    # Optionaler dritter Wert = Modifier (z. B. cmd fuer Mehrfachauswahl)
                    mods = zahlen[2] if len(zahlen) > 2 else "-"
                    ok, zeilen = hook.send(f"{k} {p} {px(zahlen[0])} {py(zahlen[1])} {mods}", 40)

            elif befehl == "taste":
                ok, zeilen = hook.send("key 0 " + rest)
            elif befehl == "tippe":
                ok, zeilen = hook.send("text 0 " + rest)
            elif befehl == "namen":
                ok, zeilen = hook.send("tree 0", 40)
            elif befehl == "menu":
                ok, zeilen = hook.send("menu " + rest, 40)
            elif befehl == "warte":
                ok, zeilen = hook.send("warte " + rest, 40)
            else:
                ok, zeilen = hook.send(zeile[2:] if zeile.startswith("> ") else zeile, 60)

            zeige(zeilen, "   ")
            if not ok:
                fehler += 1

        hook.beenden()
        print(f"== fertig (Fehler: {fehler}) — Log: {LOG} ==")
        return 1 if fehler else 0


if __name__ == "__main__":
    sys.exit(main())
