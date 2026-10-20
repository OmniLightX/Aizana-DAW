# Tracktion recording-start error propagation

Modification notice: Aizana contributors modified the pinned Tracktion Engine
sources on 2026-10-10 to propagate recording-start and destination-file errors.
The changes are distributed as `tracktion-recording-errors.patch`; upstream
copyright and license notices remain unchanged. The relevant date is recorded
in the Git history of that patch.

`tracktion-recording-errors.patch` preserves the three reviewed Developer changes
against Tracktion Engine `0a5f4e6a5f53d09c89b414a44386a12df7fa1ec6`.
It also corrects three destination-file error returns in `WaveInputDevice.cpp`:
these must use `tl::unexpected`, because a plain `String` implicitly constructs
`File` and is otherwise interpreted as a successful `expected<File, String>`.
The read-only-directory regression exposed this second part of the same failure.

`EditPlaybackContext::prepareForRecording` returns the result of `startRecording`.
Both branches of `TransportControl::performRecord` reject failures with a warning.
Starting from stopped also clears playing flags and stops the playhead. Starting
from playback leaves playback running, but does not claim recording succeeded.

CMake applies this patch before loading Tracktion's module targets. The helper
checks SHA-256 hashes of all four files first. It accepts either the exact
upstream files or the exact patched files. Unexpected or mixed contents stop
configuration without overwriting local edits. Reconfiguration is idempotent.
The submodule worktree consequently has four expected modifications; the
versioned patch and configure hook are what make them reproducible after cloning.

Initialize the two required submodules (the nested Tracktion JUCE is not used):

```sh
git submodule update --init extern/JUCE extern/tracktion_engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Aizana engine_tests
python3 tests/test_recording_patch.py
build/tests/engine_tests_artefacts/Release/engine_tests '[recording-start]'
```

The regression test uses Tracktion's hosted device and synthetic audio, with
writable and read-only destinations, from stopped and already-playing transport.
It never opens a microphone. Real CoreAudio input, permissions, device changes
and monitoring still require a hardware test.

When upgrading Tracktion, review whether upstream includes equivalent handling.
Update or retire the patch and its expected hashes together; never bypass a hash
failure by blindly replacing local dependency files.
