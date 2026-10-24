# Tracktion recording-start error propagation

Modification notice: Aizana contributors modified the pinned Tracktion Engine
sources on 2026-10-10 to propagate recording-start and destination-file errors.
The changes are distributed as `tracktion-recording-errors.patch`; upstream
copyright and license notices remain unchanged. The relevant date is recorded
in the Git history of that patch.

`tracktion-recording-errors.patch` preserves the three reviewed source changes
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

## Recording monitoring and overlap playback

On 23 October 2026, Aizana added two further patches against the same pinned
engine. `tracktion-recording-monitoring.patch` limits recording-time clip
muting to the actual capture window, including punch boundaries. It applies
after the recording-error patch.

`tracktion-recording-overlap.patch` adds a graph-build callback for clip playback
exclusions, uses Tracktion's timed muting node, and rebuilds playback when the
persisted recording priority changes (including Undo/Redo). Aizana uses this
to preserve all source clips while playing the preferred recording in overlap
regions. The callback is evaluated outside audio processing; its ranges are
sorted and merged before the realtime node receives them.

Both patches use checked source hashes and reject unexpected edits. The full
chain and repeat configuration are covered by `test_recording_patch.py`.
Upstream copyright and GPL/commercial license notices are retained.

## JUCE CoreAudio restart (23 October 2026)

`juce-coreaudio-async-restart.patch` targets JUCE commit
`19edd538429c93d277bf95b55aaa7e3eb545f951`. CoreAudio device notifications
schedule the combiner restart on its message-thread timer instead of closing
and notifying audio callbacks on the HAL notification thread. Explicit close
cancels the timer; a queued notification cannot reopen an inactive device.
Callback selection is protected by the callback lock. This is a local targeted
fix, not an upstream JUCE upgrade.

`cmake/ApplyJuceCoreAudioPatch.cmake` applies the patch before JUCE configuration,
accepts the exact patched state, and rejects unknown source changes.
`python3 tests/test_coreaudio_patch.py` exercises the production callback bodies
with a fake device and verifies application, idempotence and edit preservation.
A real Bluetooth/input-output device-switch test remains necessary.

Related upstream report: [CoreAudio device-switch race](https://forum.juce.com/t/data-race-when-switching-audio-device-sample-rate/65502).

## Render-job lifetime (24 October 2026)

`tracktion-render-job-lifetime.patch` targets the same pinned Tracktion version.
A cancelled render job can be queued without currently running. Releasing its
self-reference based only on `isRunning()` left a freed pointer in JUCE's worker
queue. The completion callback now waits until the pool no longer contains the
job; a cancellation before the deferred start prevents enqueueing altogether.
Expected cancellation of a blocking callback completes as a cancelled job,
while unexpected exceptions retain their diagnostics. This changes background
and message-thread lifecycle handling, not audio processing.

`ApplyTracktionRenderJobPatch.cmake` verifies both affected source hashes before
applying anything and preserves unknown local edits. Application, repeat
configuration, missing sources and mixed/modified states are checked by
`python3 tests/test_render_job_patch.py`. The C++ regression occupies every worker
and cancels a queued job, and separately cancels before its start message.
The explicit `[.warp-stress]` probe exercises rapid Warp edits and session swaps.
Upstream copyright and license notices remain unchanged.
