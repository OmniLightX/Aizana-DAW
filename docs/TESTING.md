# Testing Aizana

Build instructions are in [README.md](../README.md#build-from-source).
The latest recorded results are in [STATUS.md](../STATUS.md); the
[interaction checklist](LIVE_TEST_CHECKLIST.md) distinguishes completed manual
checks from remaining work. These documents report observed coverage, not a
promise of compatibility with every device or plugin.

## Automated checks

Close Aizana before running engine tests: the test executable shares audio-device
settings and plugin caches with the application. Back up those settings and use
disposable sessions. Do not run engine and interactive tests concurrently.

After configuring and building the targets described in the README:

```sh
python3 tests/test_recording_patch.py
python3 tests/test_state_patch.py
python3 tests/test_coreaudio_patch.py
ctest --test-dir build --output-on-failure
```

Use a separate Debug build for the JUCE diagnostic gate. CTest invokes
`tests/RunEngineTests.cmake`, which rejects a nonzero exit, JUCE assertion or
leaked-object report, including output during process shutdown. A passing Release
run alone does not establish Debug assertion cleanliness. Hidden diagnostic
probes are intentionally failing controls, not ordinary passing test cases.

The recording-patch suite has six checks, including the monitoring/overlap
patch chain; the state-patch suite has four. Both operate on isolated copies of the dependency sources.
They verify exact upstream/patched hashes, idempotence and rejection of mixed or
unexpected local modifications. The build must never silently overwrite an
unknown dependency state. See [patch provenance](../patches/README.md).

## Clean CI build

The prepared [macOS build and tests workflow](ci/build.yml) checks out pinned submodules
on an Apple Silicon macOS runner, installs CMake/Faust, builds the Release app and
test targets, verifies dependency patches and runs CTest. Once installed as `.github/workflows/build.yml`, it runs on main pushes,
pull requests and manual dispatch, with read-only repository permissions. Runner
images and Homebrew tools evolve; their versions are logged. This is a repeatable
clean build procedure, not a claim of byte-identical binary reproducibility.
No signing credentials, private plugins or personal recordings are required.
The workflow does not replace the separate local Debug diagnostic run.

The current GitHub credential lacks the `workflow` scope, so the file is a
reviewable template under `docs/ci/`, not an active Actions workflow. Installation
requires a credential authorized to modify workflows. No hosted CI result is
claimed before activation and the first completed run.

## Optional plugin and model fixtures

VST3 and CLAP integration tests require explicitly selected installed plugins:

```sh
export AIZANA_TEST_VST3='/path/to/selected.vst3'
export AIZANA_TEST_AU='AudioUnit:Effects/aufx,lmtr,appl'
export AIZANA_TEST_CLAP='/path/to/selected.clap'
ctest --test-dir build --output-on-failure
```

Unselected or unavailable fixtures are reported as skipped. Third-party plugin
binaries, license keys, personal sessions and source recordings are not fixtures
to commit. Apple's AUPeakLimiter is the default macOS AU test fixture.
Physical MIDI devices require a separate hardware check.

Real-model numerical tests use the exact artifact documented in
[the model report](OPEN_STEM_RELEASE.md). Synthetic short-input tests do not
replace those checks or listening comparisons. Export reproduction and optional
FFmpeg verification are described in [MP3 export](MP3_EXPORT.md).

## Regression coverage

| Area | Checks and retained invariants |
|---|---|
| Recording | Recording-start/file errors propagate; failed recording is not reported as successful. Real CoreAudio input and device changes need hardware testing. |
| Session storage | First save gathers required media; cancelled/failed saves preserve the current session and existing destination. Backups, recovery, reopening and legacy paths have dedicated checks. |
| Mixer and tracks | Selection and hit areas, grouped faders, pan after reload, duplication with independent IDs, track order, bus routing, Undo/Redo and save/reload. |
| Automation | Text-valued Tracktion mode decoding, first dropdown selection, menu/lane synchronization, parameter selection, Undo/Redo and preservation of curves. |
| Clips and comping | Split/trim/gain, selection bounds, selected takes and effective comp waveform, rendered length and Undo/Redo. |
| Inserts | Copy/move preserves the appropriate state; copied instances receive new IDs. Routing/instrument plugins stay protected; sidechain and timeline-automation copy limits remain documented in the manual. |
| Editor lifetime | Floating and docked wrappers own native editors. Destruction tests verify editor deletion; worker metadata calls run on the message thread. |
| Worker failure | Disconnect callbacks guard processor lifetime, failed loads are reported, and controlled worker crashes exercise restart/state recovery in the OOP smoke test. |
| Bus layouts | Missing/malformed metadata does not request invalid layouts; dynamic sidechain changes restore through Undo/Redo. |
| Edit teardown | Plugin state is not flushed after Edit invalidation; scene setup avoids nested Undo mutations during track insertion. |
| Export | PCM reference signals, sample rates, stereo content, duration, MP3 bitrate/delay metadata, cancellation and preservation of existing files. |
| Stem jobs | Four/six output modes, numerical parity, cancellation cleanup and suppression of late results after a session switch. |

Native checks with Fresh Air covered one usable OOP editor, close/reopen and
session switching. Inf Horizon normal-hosting editor close/reopen, track creation
and process exit were also checked after ownership corrections. These are
specific tested workflows, not certification of either plugin in all modes.
Default hosting remains in-process; all-plugin OOP is opt-in.

## Native interface testing

Use disposable sessions and audio you are permitted to share. The dedicated
[UI-test hook](UITEST_HOOK.md) is excluded from normal builds and only addresses
its own process. Native dialogs, system keyboard handling and third-party plugin
windows require separate interaction checks. A dispatched command is not proof
that the visible result is correct; inspect both the interface and saved state.

## Bundle checks

```sh
python3 scripts/audit-release.py build/Aizana_artefacts/Release/Aizana.app
```

This checks selected local-path markers, deployment targets, external library
load paths and required license files. It does not establish notarization,
first launch on another Mac, complete license compliance or universal plugin
compatibility. Production builds must use `AIZANA_UITEST_HOOK=OFF`.

The `[recording-waveform]` test uses a disposable edit and simulated audio input.
It renders the same live-thumbnail drawing function used by the arrangement to
an image before recording stops, checks waveform pixels (not just the region
background), and covers normal recording, punch, looping, scrolling, and stop.
This does not replace the microphone and arrangement interaction checks.

`test_coreaudio_patch.py` executes the patched restart notification and timer
bodies against a fake device: notification must only schedule work, and a closed
device must not restart. It also checks clean patch application, repeated
configuration, and preservation of unknown local edits. This is a control-flow
regression test, not a substitute for Bluetooth hardware switching.
