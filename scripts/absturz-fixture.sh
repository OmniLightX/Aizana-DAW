#!/bin/bash
# Baut den Aufbau fuer Drehbuch 39 (Absturz-Wiederherstellung).
#   $1 = Ordner fuer die Session   $2 = Datei, die als Schnappschuss dient
set -e
BASE="$1"; ANDERS="$2"
mkdir -p "$BASE/Absturztest/Sicherungen"
cp "$ANDERS" "$BASE/Absturztest/Sicherungen/Absturztest_Wiederherstellung.tracktionedit"
touch "$BASE/Absturztest/Absturztest.tracktion" 2>/dev/null || true
python3 - "$BASE" <<'PY'
import json, os, sys, pathlib, time
base = pathlib.Path(sys.argv[1])/"Absturztest"
snap = base/"Sicherungen"/"Absturztest_Wiederherstellung.tracktionedit"
t = time.time()+30
os.utime(snap, (t, t))
marker = pathlib.Path(os.path.expanduser("~/Library/Aizana/offene-sitzung.json"))
marker.parent.mkdir(parents=True, exist_ok=True)
marker.write_text(json.dumps({"session": str(base/"Absturztest.tracktion"),
                              "snapshot": str(snap)}))
print("Aufbau fertig:", base)
PY
