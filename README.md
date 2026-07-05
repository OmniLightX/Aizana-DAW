# Aizana

An open-source, **AI-first DAW** built on JUCE 8 and Tracktion Engine for macOS. Mixing-first design: familiar enough that anyone starts immediately, with AI that turns intent into normal, reviewable, one-undo DAW actions — built for beginners and pros over the same session.

> **Status: Public Alpha / Developer Preview.** Aizana is real and works, but it is
> not a finished v1.0. A DAW is high-trust software (it records performances, hosts
> third-party plugins, writes files) — treat it as a preview, keep backups, and see
> *Known limitations* below. This is not a replacement for Logic, Studio One, Pro
> Tools, Ableton, Cubase, or REAPER.

Aizana source code is licensed under GPL-3.0-or-later.

Binary/source distributions also include third-party components under their own
licenses. In particular, JUCE 8 is AGPLv3/Commercial, and Tracktion Engine is
GPL3-or-later/Commercial. Public Aizana releases therefore need to comply with
the applicable copyleft obligations for those components. See
`THIRD_PARTY_NOTICES.md` for the dependency and model-weight notices.

## Features

- Multi-track recording, editing, and mixing
- **AI mixing (heuristic co-pilot):** one "AI Mix" button proposes gain staging,
  EQ, compression, de-essing, panning, and sub-bus routing as named, jargon-free,
  reviewable moves — Beginner applies instantly, Pro reviews each move; one undo step
- **AI stem separation:** split a full song into stems (drums, bass, vocals, guitar,
  piano, synths) and get a ready-routed session — one undo step
- **AI session setup:** import stems, instruments are detected and grouped with
  routing and gain staging applied automatically
- **Local-first AI:** inference runs on-device via ONNX Runtime + CoreML (Apple
  Neural Engine); no audio leaves your computer. A local training-data flywheel
  records reviewed mix decisions so the AI can learn your taste over time
- VST3 / AU / **CLAP** plugin hosting with floating editors
- Parametric EQ, Compressor, Reverb, Limiter, and a split-band De-Esser built-in
- Piano Roll with MIDI editing and velocity lanes
- Automation lanes with Read / Touch / Latch / Write modes
- Clip timestretch: conform any clip to the song tempo (SoundTouch)
- Export to WAV / AIFF (mixdown and stems)

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| CMake | 3.25+ | Build system |
| Xcode | 14+ | macOS toolchain (Command Line Tools sufficient) |
| JUCE | 8.x | Included as submodule at `extern/JUCE` |
| Tracktion Engine | 3.x | Included as submodule at `extern/tracktion_engine` |

## Building

```bash
# Clone with submodules
git clone --recurse-submodules <repo-url>
cd aizana

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (replace 8 with your core count)
cmake --build build --config Release -j8
```

The app bundle is written to `build/Aizana_artefacts/Release/Aizana.app`.

### Universal Binary (Apple Silicon + Intel)

```bash
cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j8
```

The first `cmake -B build` downloads dependencies via `FetchContent`, including a
prebuilt ONNX Runtime (~16 MB) for the local inference backend. To build without
it, configure with `-DAIZANA_ENABLE_ONNX=OFF` (the AI backend then compiles as a
defined no-op and Aizana falls back to its heuristics).

## Known Limitations

- **macOS only** (CoreAudio; no Windows/Linux build).
- **Public alpha** — expect rough edges; session-format stability is not yet
  guaranteed across versions. Keep backups; don't use on irreplaceable client work yet.
- **AI mixing is heuristic** today (Grammy-informed rules). A trained local model is
  in progress — the inference seam is in place but ships without a bundled model, so
  mixing currently runs on the heuristics.
- **Plugin compatibility** across the full VST3/AU/CLAP ecosystem is not yet
  exhaustively tested.
- **Model weights are not bundled** (see below) — stem separation downloads them on
  first use.

## AI Model Weights

Aizana' stem separation uses the Demucs v4 hybrid-transformer models
(© Meta / Facebook Research, [facebookresearch/demucs](https://github.com/facebookresearch/demucs)),
run locally through the MIT-licensed [demucs.cpp](https://github.com/sevagh/demucs.cpp)
inference library (vendored at `extern/demucs_cpp`).

**The model weights are not part of this software.** They are not bundled in
the app, not stored in this repository, and never distributed by the Aizana
project. On first use, Aizana offers to download them directly from public
hosting onto your machine; that download happens between you and the hosting
provider. All inference runs locally — no audio ever leaves your computer.

Licensing note: the Demucs *code* is explicitly MIT-licensed; Meta has not
published an explicit license statement for the pretrained *weights*
(see [facebookresearch/demucs#327](https://github.com/facebookresearch/demucs/issues/327)).
Aizana therefore only links to the publicly hosted files instead of
redistributing them. The GPL-3.0 license of Aizana covers the Aizana source
code only and does not extend to the model weights.

## Project Structure

```
src/
  app/        — MainComponent, application entry
  ai/         — StemAnalyzer, SessionPlanner, StemImport/SeparationEngine
  audio/      — AudioEngine, recording
  engine/     — MixerEngine, ClipEditor, ExportPipeline, AICommandBridge
  fx/         — EQ, Compressor, Reverb, Limiter DSP
  ui/         — ArrangementView, MixerView, PianoRoll, Automation
extern/
  JUCE/
  tracktion_engine/
  demucs_cpp/ — vendored MIT inference library for stem separation
```
