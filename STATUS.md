# Aizana 0.3.0 Alpha — test status

Recorded checks through 24 October 2026.
Target: Apple Silicon (arm64), macOS 13.3 or later. Aizana is distributed as
source code. This page records completed runs; it does not imply a fresh full
suite for every documentation-only commit.

## Automated results

- Generator report follow-up: 14 targeted Debug cases, 83 assertions passed,
  including real Faust/Clang compilation, special-character display names,
  host parameter/audio response, timeout/output limits and preservation of an
  existing plugin after a failed compile. The JUCE diagnostic gate was clean.
  Recording/state/CoreAudio patch tests passed (6/4/2). Release app build and
  bundle audit passed. The subsequent full Debug suite passed 467 cases,
  6 skips and 50,866 assertions, with a clean JUCE diagnostic gate. The GitHub
  workflow is now active; hosted Release run
  [40039334331](https://github.com/OmniLightX/Aizana-DAW/actions/runs/40039334331)
  and independent rerun
  [40041084368](https://github.com/OmniLightX/Aizana-DAW/actions/runs/40041084368)
  passed the build, patch checks and both CTest targets.

- Warp cancellation: a focused AddressSanitizer stress probe reproduced a
  heap-use-after-free in JUCE's worker queue. Tracktion released a cancelled
  render job while it was still queued. The lifecycle patch now retains the
  job until the pool releases it and prevents enqueueing after cancellation.
  Expected cancellation of blocking callbacks follows the normal failure
  completion path. Warp readiness also requires the output file to exist.
  The previously crashing probe plus Warp checks passed 305 assertions in
  8 cases under ASan with clean JUCE diagnostics. Deterministic queue and
  missing-output regressions have been added. The complete post-fix Debug suite
  passed 469 cases, 6 skips and 50,879 assertions with a clean diagnostic gate.
  The complete local Release suite also passed 469 cases, 6 skips and 50,879
  assertions; forty Release stress rounds passed 240 assertions. Release bundle
  audit passed. The complete post-fix ASan diagnostic suite passed 464 cases,
  11 skips and 50,831 assertions, with ONNX/generator disabled only in that
  diagnostic configuration. Hosted run
  [40044378452](https://github.com/OmniLightX/Aizana-DAW/actions/runs/40044378452)
  on `f63c60d7` passed the fresh Release build, all 16 patch checks, both CTest
  targets and the additional forty-round Warp stress check. The earlier CI SIGSEGV had no stack, so an identical cause cannot
  be established from that old log alone.

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

- Latest full Debug engine suite: **469 passed, 6 skipped, 50,879 assertions**,
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

Model delivery verified on 24 October 2026: anonymous full download and first-use
download through the ordinary app dialog both installed the expected model bytes
(size and SHA-256 checked).
