#!/bin/bash
# Faust DSP template -> self-contained universal .clap (Phase 4 / Iteration B, B1).
# Invoked by CMake at build time per template. Recipe proven in Spike S2.
#   args: <faust> <dsp> <out.clap> <archdir> <faust_include> <clap_inc> <helpers_inc> <globals.cpp> <work_dir>
set -euo pipefail

FAUST="$1"; DSP="$2"; OUT="$3"; ARCHDIR="$4"; FAUST_INC="$5"
CLAP_INC="$6"; HELP_INC="$7"; GLOBALS="$8"; WORK="$9"

BASE="$(basename "$DSP" .dsp)"
mkdir -p "$WORK" "$(dirname "$OUT")"

cat > "$WORK/plugin_metadata.h" <<META
#define FAUST_PLUGIN_ID "com.aizana.gen.$BASE"
#define FAUST_PLUGIN_NAME "$BASE"
#define FAUST_PLUGIN_VENDOR "Aizana"
#define FAUST_PLUGIN_VERSION "1.0.0"
#define FAUST_PLUGIN_DESCRIPTION "Aizana generated plugin"
#define FAUST_NVOICES 16
#define FAUST_IS_POLYPHONIC 0
META

# Faust DSP + CLAP architecture -> standalone C++
"$FAUST" -a "$ARCHDIR/clap/clap-arch.cpp" "$DSP" -o "$WORK/${BASE}_clap.cpp"

# Compile/link -> universal, self-contained .clap (clap-helpers-impl = template instantiation;
# faust_gui_globals = the one-time GUI::gTimedZoneMap definition).
clang++ -std=c++17 -O2 -fPIC -shared -arch arm64 -arch x86_64 -mmacosx-version-min=13.3 \
  -Wl,-install_name,"@rpath/${BASE}.clap" \
  -ffile-prefix-map="$WORK"=generator-work \
  -I"$FAUST_INC" -I"$CLAP_INC" -I"$HELP_INC" -I"$WORK" \
  "$WORK/${BASE}_clap.cpp" "$ARCHDIR/clap/clap-helpers-impl.cpp" "$GLOBALS" \
  -o "$OUT"

echo "[generator] built $OUT"
