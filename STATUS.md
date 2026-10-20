# Aizana 0.3.0 Alpha — development and test status

As of 20 October 2026. Aizana is provided as source code for users to build.
Build target: Apple Silicon (arm64), macOS 13.3 or later.

## Verified checks

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
