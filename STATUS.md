# Aizana 0.3.0 Alpha — development and test status

As of 20 October 2026. Aizana is provided as source code for users to build.
Build target: Apple Silicon (arm64), macOS 13.3 or later.

## Track-header automation menu — 20 October 2026

- Right-click Automation submenu: show/hide lane, select an available parameter,
  and choose Read/Touch/Latch/Write. The existing A button remains.
- Visibility and parameter selection do not change automation mode or curves.
  Menu mode changes have their own Undo transaction; an open lane follows
  external mode changes and parameter selection without notification loops.
- A user-discovered display regression was reproduced in a real AutomationLane
  test (6 failing assertions): TE stores automation mode as text, not an integer.
  Correct decoding fixes external mode updates and the first dropdown selection.
  The regression test now passes all 21 assertions, including mode Undo/Redo,
  parameter-selector synchronization and unchanged curve data.
- Corrected Release app builds and bundle audit pass. Final full Debug suite:
  459 passed, 6 skipped, 50,219 assertions, exit 0. Corrected Release app opened
  for the user; the user confirmed first-selection mode changes work correctly
  both in the context menu and in the automation lane.

## Mixer interactions and track actions — 20 October 2026

- Narrow fader/pan hit areas; horizontal pan dragging; channel selection on
  background/control clicks without losing existing multiple selections.
- Drag regular channels within their current parent to reorder; duplicate full
  tracks from mixer or arrangement menus with independent IDs and Undo/Redo.
  Output routing, shared source media and effect/automation state are covered
  through duplication, reordering, Undo/Redo and save/reload.
- Resize the shared lower dock from its upper boundary; bottom switches remain.
- Full Debug suite at mixer integration: 458 passed, 6 skipped, 50,198 assertions, exit 0; no JUCE
  assertion/leak diagnostics. New Release subset: 8 cases, 162 assertions,
  exit 0. Debug/Release app builds and Release bundle audit passed.
- Native Release checks passed for blank-area selection, unchanged values beside
  controls, horizontal pan, upper divider resize, grouped faders, numeric entry,
  reorder with arrangement synchronization, both duplicate menus and menu Undo.
- User confirmed reorder Undo/Redo, track copying with drawn automation,
  retained pan and bus assignment during direct testing.
- Additional read-only code review found no verified blocker or major issue.
  Remaining native edge cases are recorded in the live checklist.

## MP3 export — 20 October 2026

- Built-in LAME 3.100 encoding for stereo mixdown and stems: 44.1/48 kHz,
  128/192/256/320 kbit/s CBR. Exact upstream source and license are bundled.
- Full Debug engine suite at MP3 integration: 450 passed, 6 skipped, 50,036 assertions;
  no JUCE assertion/leak diagnostic, process exit 0. Optional/plugin and
  hardware skips were not reclassified as passes in this run.
- Export subset: Debug 10 cases / 293 assertions (including retained-artifact
  checks); Release 10 cases / 273 assertions. Both exit 0, covering WAV/AIFF
  reference signals, MP3 decoding, stems, errors and cancellation.
- Debug and Release app builds pass. Release bundle audit passes privacy,
  dependency and license checks; LAME license texts are present and there is
  no external libmp3lame/Homebrew runtime dependency.
- Native dialog interaction was not run: a user Aizana instance was active at
  the pre-launch check. It was neither replaced nor closed; no second app was
  launched.
- Independent FFmpeg decoding: all eight MP3 rate/bitrate combinations identify
  as MP3 stereo at the requested bitrate and decode to exactly one second
  (44,100 or 48,000 frames). Generated left/right reference tones remain distinct.
- Existing output files are replaced only after successful completion. Cancelled
  or failed files preserve the previous destination; finished stems remain.
- See [implementation and test reproduction](docs/MP3_EXPORT.md) and the separate
  [native interaction checklist](docs/LIVE_TEST_CHECKLIST.md).

## Verified checks before MP3 integration

- Debug and Release: 444 engine test cases and 49,837 assertions passed in each.
  Six cases were initially skipped. The optional VST3/CLAP checks were
  subsequently run with installed plugins: eight selected cases and 35
  assertions passed per configuration, with no skips. One case for another
  platform and one requiring an unavailable MIDI device system remain excluded.
- No JUCE assertion or leak report in these runs. The separate
  OOP smoke test passes in both configurations.
- Fresh Air: only the native plugin window is visible, and controls are usable;
  opening, closing, reopening and session switching were checked. The previous
  plugin process terminates when switching sessions.
- Track creation, Undo/Redo, saving and reopening were checked. A save destination
  that is no longer writable reports an error and preserves the existing file.
- Moving a single insert between two audio channels with Option and copying it
  to a bus without Option were checked. Parameter state and Undo/Redo are
  verified for the documented cases.
- App builds and bundle audit passed. The model, license file and checksum list
  were downloaded in full and compared with the original files.

## Not yet fully accepted

- Anonymous model retrieval and first download through the regular app dialog.
- Physical MIDI keyboard and hardware cue outputs.
- Other third-party OOP editors and their sound comparison; Fresh Air acceptance
  does not guarantee that every plugin works.
- Insert combinations and visual toggle states not yet individually verified;
  chain/preset checks that have already passed are listed in the live checklist.
- First launch on an independent Mac. No notarized app package is available.

## Documentation

- [User manual](MANUAL.md)
- [Current live checks](docs/LIVE_TEST_CHECKLIST.md)
- [OOP windows and regression tests](docs/OOP_EDITOR_WINDOWS_2026-10-20.md)
- [JUCE diagnostics and fixes](docs/DEBUG_DIAGNOSTICS_2026-10-19.md)
- [Model provenance and checks](docs/OPEN_STEM_RELEASE.md)
- [Components and licenses](THIRD_PARTY_NOTICES.md)

AI mixing uses built-in analysis and mixing rules. Optional stem separation
uses the documented MIT model. The application as a whole is not MIT-licensed;
in particular, GPL and AGPL conditions also apply.
