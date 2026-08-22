<p align="center">
  <img src="Resources/brand/aizana-banner.png" alt="Aizana by Aizanagi — open-source AI-assisted Digital Audio Workstation" width="820">
</p>

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

## Highlights

### ⭐ Aizana AI Studio — an AI assistant *inside* the DAW

A chat panel in the transport bar that connects to your locally installed
**Claude Code** or **Codex CLI** (local-first, no API key handled by Aizana) and
acts on your session:

- **Mix by conversation:** the assistant controls the *running* session over a
  local loopback bridge — track volumes/pan, mute/solo, EQ, compressors,
  de-essers, reverb sends, bus creation (14 live tools). Every batch is one
  undo step.
- **Generate real plugins from a text description:** the assistant writes
  Faust DSP, Aizana compiles it to a universal **CLAP** plugin with a working
  parameter GUI, and it lands in your plugin library under "Aizana" —
  persistent and reusable like any other plugin. No cloud compile, no account.

### AI mixing & session setup

- **AI Mix button (heuristic co-pilot):** proposes gain staging, EQ,
  compression, de-essing, panning, and sub-bus routing as named, jargon-free,
  reviewable moves — Beginner applies instantly, Pro reviews each move; one
  undo step.
- **AI stem separation:** split a full song into stems (drums, bass, vocals,
  guitar, piano, synths) locally via Demucs v4 and get a ready-routed session.
- **AI session setup:** drop in stems, instruments are detected and grouped
  with routing and gain staging applied automatically.
- **Reference system:** learn tonal balance and level targets from a reference
  track (or genre preset); nudges gain staging and gently tilts the master EQ —
  guidance, never forced.
- **Local-first AI:** inference runs on-device (ONNX Runtime + CoreML / Apple
  Neural Engine); no audio leaves your computer. A local training-data flywheel
  records reviewed mix decisions so the AI can learn your taste over time.

### Instruments & built-in FX

- **Aizana Drums:** drum machine with 8×16 step sequencer, pad grid, and a
  **WAV sampler** — load your own kick/snare/hats per pad or map a whole kit
  folder; patterns run transport-synced and persist with the session.
- **Aizana Synth:** 2-oscillator subtractive synth (filter env, ADSR, 16
  voices) with factory presets.
- **8 built-in effects:** parametric EQ (with spectrum analyzer), Compressor
  (with sidechain), Reverb, Limiter, split-band De-Esser, tempo-synced Delay
  (ping-pong), Gate (with sidechain key), and a Tape/Tube/Soft Saturator.

### Recording, editing & mixing

- Multi-track recording with loop **takes and comping**, punch in/out, and
  input monitoring
- **Seamless count-in:** the transport rolls through the count bars and punches
  in sample-accurately, with no stop or gap at the switch. The click is
  separately switchable for playback and for recording, so it can count you in
  and then fall silent (vocals) or keep running (beat making)
- Studio One-style **docked editor** (Edit | Mix | Browse) with dedicated
  audio-clip and MIDI editing workspaces
- **Audio bend/warp** (transient-snapped warp markers), **tempo track** with
  time-signature changes, and **tempo-map extraction** from freely played audio
- **Vocal tuning** (offline, scale-snapped pitch correction), clip timestretch
  ("conform to song tempo"), and **clip gain envelopes**
- Piano Roll with velocity lanes, scale lock, CC lane, and a **quantize bar**
  (grid / strength / swing)
- Automation for **every plugin parameter** (Read / Touch / Latch / Write),
  fades, crossfades, clip trim/gain, edit groups, bounce-in-place
- Arrangement tools: pointer, range, scissors, eraser and **mute tool**, plus
  **merge events** (⌘J) — glues clips into one, natively for MIDI and via an
  event-only render for audio, in a single undo step
- Full mixing console: buses, FX channels with sends (incl. bus→bus),
  VCAs, sidechain routing, **cue mixes** to separate hardware outputs
  (pre-fader), LUFS/peak/RMS metering
- Adaptive interface: **Beginner and Pro views** over the same session, with a
  first-run wizard so you never start on a blank screen
- VST3 / AU / **CLAP** plugin hosting with floating editors; experimental
  **out-of-process hosting** (crash- and hang-isolated plugins, opt-in via
  environment flag)
- Export to WAV / AIFF (mixdown and stems) at 44.1/48/96 kHz

### Sessions, files & crash safety

Everything belonging to a session lives in one folder next to it, the way Studio
One and Logic organise a project:

```
My Song/
    My Song.tracktion        the session
    My Song Audio/           recordings, plus Comps/ and Bounces/
    Backups/                 backups + crash snapshot
```

- **Recordings, comps and bounces are written into that folder** — never loose
  next to the session or into the system temp directory.
- **Rotating autosave** (interval configurable, three slots) to step back to an
  earlier state.
- **Crash recovery:** a cheap state snapshot is written every minute and a marker
  is cleared on clean shutdown. If the marker survives, the last session ended in
  a crash — only then does Aizana offer to restore, and only if the snapshot is
  newer than the saved session. Restoring writes back to the session, not into
  the backup folder.

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| CMake | 3.25+ | Build system |
| Xcode | 14+ | macOS toolchain (Command Line Tools sufficient) |
| JUCE | 8.x | Included as submodule at `extern/JUCE` |
| Tracktion Engine | 3.x | Included as submodule at `extern/tracktion_engine` |

Optional, for the AI Studio assistant: a locally installed
[Claude Code](https://claude.com/claude-code) or Codex CLI. Optional, for
generating plugins: the [Faust](https://faust.grame.fr) compiler.

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

- **macOS only** (CoreAudio; no Windows/Linux build). Prebuilt binaries are
  currently Apple Silicon (arm64) — Intel Macs need a universal rebuild (see
  above).
- **Public alpha** — expect rough edges; session-format stability is not yet
  guaranteed across versions. Keep backups; don't use on irreplaceable client work yet.
- **AI mixing is heuristic** today (Grammy-informed rules). A trained local model is
  in progress — the inference seam is in place but ships without a bundled model, so
  mixing currently runs on the heuristics.
- **Plugin compatibility** across the full VST3/AU/CLAP ecosystem is not yet
  exhaustively tested; out-of-process hosting is experimental and off by default.
- **Model weights are not bundled** (see below) — stem separation downloads them on
  first use.
- **The interface is largely German** at this point, with English left in places.
  A consistent language pass (and a language switch) is still open. Folder names
  Aizana writes to disk are English (`<Name> Audio`, `Comps`, `Bounces`,
  `Backups`), the way Studio One and Logic keep them English on a German system.

## AI Model Weights

Aizana's stem separation uses the Demucs v4 hybrid-transformer models
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
  app/         — application entry, main shell, legacy migration
  ai/          — StemAnalyzer, SessionPlanner, StemImport/SeparationEngine,
                 MixingEngine, AiAgentBridge, live-session AI server
  audio/       — AudioEngine, recording
  engine/      — MixerEngine, ClipEditor, TempoEngine, WarpEngine,
                 VocalTuner, ExportPipeline, AICommandBridge
  fx/          — 8 built-in effects (EQ, Comp, Reverb, Limiter, De-Esser,
                 Delay, Gate, Saturator)
  instruments/ — Aizana Drums (sequencer + sampler), Aizana Synth
  generator/   — text → Faust → CLAP plugin generator
  plugins/     — plugin scanning/hosting, out-of-process hosting
  mcp/         — aizana-mcp server (AI Studio tool backend)
  metering/    — LUFS/peak/RMS metering
  ui/          — arrangement, mixer, docked editor, piano roll, AI Studio,
                 beginner view, onboarding, comping, automation
extern/
  JUCE/
  tracktion_engine/
  demucs_cpp/  — vendored MIT inference library for stem separation
```
