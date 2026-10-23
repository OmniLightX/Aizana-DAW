# Aizana 0.3.0 Alpha — test status

Recorded checks through 23 October 2026.
Target: Apple Silicon (arm64), macOS 13.3 or later. Aizana is distributed as
source code. This page records completed runs; it does not imply a fresh full
suite for every documentation-only commit.

## Automated results

- CoreAudio combined-device restart: deferred shutdown regression and patch
  application/idempotence tests passed. Debug suite again passed 465 cases,
  6 skips, 50,838 assertions with clean JUCE diagnostics; Release build and
  bundle audit passed. In the rebuilt app, two round trips between AirPods Pro
  output and built-in speakers with built-in microphone input completed without
  a crash. Switching during playback, physical disconnect/reconnect and interface
  recording remain live checks; this is not a general crash-immunity claim.

- Live recording waveform: simulated-input drawing test passed 45 assertions
  across normal, punch and loop recording, including scrolling and stop.
  Release app build and bundle audit passed. The subsequent full Debug suite
  passed with clean JUCE diagnostics. Real-device/UI verification remains pending.

- Successive recording playback: rendered audio verifies new-take priority in
  partial overlaps, original audio outside the overlap, previous-take selection,
  Undo/Redo and save/reload. Recording-start tests also verify automatic priority
  assignment after a real synthetic-input recording. Loop-take containers and
  unrelated tracks are covered. Debug tests and the Release app build passed;
  native menu and microphone checks for this playback change remain pending.

- After removal of the legacy Demucs library and separate Eigen build dependency:
  Debug and Release app/test builds passed. The full Debug suite again passed
  459 cases with 6 skips and 50,219 assertions. Real Mega 53 reference/output
  tests passed 125 assertions each in Debug and Release; ordinary Release stem tests passed
  58 assertions. Both app bundles retain ONNX notices and exclude the old
  standalone Demucs/Eigen notice folders. The Release bundle audit passed.

- Latest full Debug engine suite: **465 passed, 6 skipped, 50,838 assertions**,
  exit 0. The diagnostic gate found no JUCE assertion or leaked-object report.
- Automation UI regression: 21 assertions passed, covering mode/parameter
  synchronization, first selection, Undo/Redo and curve preservation.
- Mixer/track-action Release subset: 8 cases, 162 assertions passed.
- MP3 export subset: Debug 10 cases / 293 assertions with retained artifacts;
  Release 10 cases / 273 assertions. Independent FFmpeg decoding checked all
  eight sample-rate/bitrate combinations, stereo channels and decoded duration.
- Recording and state-patch Python suites: four tests each. Both rerun and passed
  on 21 October during documentation maintenance.
- Corrected Debug/Release app builds and the selected Release bundle audit passed.
  The bundle audit was rerun on 21 October; eight Mach-O files checked.
- OOP smoke passed in Debug and Release. Selected VST3/CLAP integrations passed
  in separate runs; optional fixtures skipped in the full suite remain skips.

Reproduction and coverage: [Testing](docs/TESTING.md).

## Completed native checks

- Mixer free-area selection, control hit areas, horizontal pan dragging, grouped
  faders, numeric entry and resizing at the upper boundary.
- Track reordering with arrangement synchronization and keyboard/menu Undo/Redo.
- Track duplication from mixer/arrangement menus; copies retain drawn automation,
  pan and bus assignment. Automated tests additionally cover save/reload.
- Automation mode changes work on the first selection from both the track menu
  and automation lane.
- Track creation, saving/reopening, session changes and preservation of an
  existing file when saving to an unwritable destination.
- Single insert Option-move between audio channels; copy to a bus; chain copying,
  presets and bulk actions for the combinations in the interaction checklist.
- Fresh Air OOP editor opening, closing, reopening, controls and session switching.
- Stem preview/import, import Undo/Redo, cancellation and session-switch cleanup.

## Remaining checks and limitations

- Anonymous first model download through the ordinary app dialog.
- Physical MIDI keyboard and hardware cue outputs.
- Other third-party OOP editors and audio comparisons.
- Remaining insert/sidechain/preset combinations and visual toggle states.
- Mixer edge autoscroll, small/collapsed dock behavior and native track-copy
  save/reopen with audio, effects and automation together.
- Automation show/hide, parameter-selection and Undo/Redo native combinations
  beyond the confirmed mode-selection check.
- Native MP3 dialog cancellation/close behavior and real-session listening checks.
- First launch on an independent Mac. No notarized app package is provided.

Detailed cases: [interaction checklist](docs/LIVE_TEST_CHECKLIST.md).
Model-specific evidence and quality limits: [model report](docs/OPEN_STEM_RELEASE.md).
