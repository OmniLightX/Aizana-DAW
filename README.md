# Cloudes

A professional, open-source DAW built on JUCE 8 and Tracktion Engine for macOS. Mixing-first design: familiar enough that anyone starts immediately, powerful enough that professionals prefer it.

Licensed under GPL-3.0.

## Features

- Multi-track recording, editing, and mixing
- AI stem separation: split a full song into stems (drums, bass, vocals, guitar,
  piano, synths) and get a ready-routed session — one undo step
- AI session setup: import stems, instruments are detected and grouped with
  routing and gain staging applied automatically
- VST3 / AU plugin hosting with floating editors
- Parametric EQ, Compressor, Reverb, and Limiter built-in
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
cd cloudes

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (replace 8 with your core count)
cmake --build build --config Release -j8
```

The app bundle is written to `build/Cloudes_artefacts/Release/Cloudes.app`.

### Universal Binary (Apple Silicon + Intel)

```bash
cmake -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release -j8
```

## AI Model Weights

Cloudes' stem separation uses the Demucs v4 hybrid-transformer models
(© Meta / Facebook Research, [facebookresearch/demucs](https://github.com/facebookresearch/demucs)),
run locally through the MIT-licensed [demucs.cpp](https://github.com/sevagh/demucs.cpp)
inference library (vendored at `extern/demucs_cpp`).

**The model weights are not part of this software.** They are not bundled in
the app, not stored in this repository, and never distributed by the Cloudes
project. On first use, Cloudes offers to download them directly from public
hosting onto your machine; that download happens between you and the hosting
provider. All inference runs locally — no audio ever leaves your computer.

Licensing note: the Demucs *code* is explicitly MIT-licensed; Meta has not
published an explicit license statement for the pretrained *weights*
(see [facebookresearch/demucs#327](https://github.com/facebookresearch/demucs/issues/327)).
Cloudes therefore only links to the publicly hosted files instead of
redistributing them. The GPL-3.0 license of Cloudes covers the Cloudes source
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
