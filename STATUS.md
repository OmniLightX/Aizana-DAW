# Aizana 0.3.0 Alpha — test status

Recorded checks through 20 October 2026; documentation consolidated 21 October.
Target: Apple Silicon (arm64), macOS 13.3 or later. Aizana is distributed as
source code. This page records completed runs; it does not imply a fresh full
suite for every documentation-only commit.

## Automated results

- Latest full Debug engine suite: **459 passed, 6 skipped, 50,219 assertions**,
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
