# Cloudes

A professional, open-source DAW built on JUCE 8 and Tracktion Engine for macOS. Mixing-first design: familiar enough that anyone starts immediately, powerful enough that professionals prefer it.

Licensed under GPL-3.0.

## Features

- Multi-track recording, editing, and mixing
- VST3 / AU plugin hosting with floating editors
- Parametric EQ, Compressor, Reverb, and Limiter built-in
- Piano Roll with MIDI editing and velocity lanes
- Automation lanes with Read / Touch / Latch / Write modes
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

## Project Structure

```
src/
  app/        — MainComponent, application entry
  audio/      — AudioEngine, recording
  engine/     — MixerEngine, ClipEditor, ExportPipeline
  fx/         — EQ, Compressor, Reverb, Limiter DSP
  ui/         — ArrangementView, MixerView, PianoRoll, Automation
extern/
  JUCE/
  tracktion_engine/
```
