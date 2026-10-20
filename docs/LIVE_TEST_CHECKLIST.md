# Aizana — live verification status

As of 20 October 2026. This overview distinguishes completed interaction checks
from combinations not yet accepted. Automated tests and native interaction
checks provide different kinds of evidence.

## Current interaction checks

| Area | Result | Evidence |
|---|---|---|
| Fresh Air OOP editor | Passed: one native window, operable controls, reopening and session change with the old worker terminated | [OOP window check](OOP_EDITOR_WINDOWS_2026-10-20.md) |
| Move a single insert | Passed: Option-drag between audio channels, state retained, Undo/Redo | [Insert check](INSERT_FEEDBACK_2026-10-19.md) |
| Copy a single insert | Passed: EQ copied to bus, source retained, separate destination instance | [Insert check](INSERT_FEEDBACK_2026-10-19.md) |
| Track creation and session | Passed: creation, Undo/Redo, saving/reopening and session change | [Session checks](RELEASE_TESTS_2026-10-20.md) |
| Save failure | Passed: unwritable destination reported, existing file retained | [Session checks](RELEASE_TESTS_2026-10-20.md) |
| Stem separation | Passed: preview, import, Undo/Redo, cancellation and session change without late import | [Stem checks](OPEN_STEM_RELEASE.md) |
| Multiple selection and mixer | Passed: selection actions, group-fader Undo and pan display after reload | [Selection](TEST_HISTORY.md), [Mixer](TEST_HISTORY.md) |

## Track-header automation menu follow-up — 20 October 2026

- [x] Existing A button still expands the lane in the new Debug build.
- [ ] Right-click header → Automation → show/hide; existing curves/mode unchanged.
- [ ] Choose Pan and a plugin parameter: curve and parameter selector agree.
- [x] User confirmed mode changes from both menu and lane work on the first
      selection in the corrected Release build.
- [ ] Undo/Redo mode change and check both menu tick and lane selector.
- User found a mode display regression: menu changes displayed Read and the
  lane dropdown required a second selection. A new real AutomationLane test
  reproduced six failing assertions. Fixed by decoding TE's text-valued mode
  through its VariantConverter; all 21 assertions then passed, covering external
  modes, first dropdown selection, parameter selector, curve preservation and
  mode Undo/Redo. User confirmed the corrected mode selection in both UI entry
  points during native retesting.
- Computer-use targeting was unreliable for the Debug app; native results are
  not inferred from compilation.
- User separately confirmed mixer reorder Undo/Redo shortcuts and duplication
  of a track containing drawn automation in voice testing.

## Mixer interaction follow-up — 20 October 2026

Automated checks cover narrow fader/pan hit geometry, splitter bounds, full
track copying, independent effect/automation IDs, media references and routing
through Undo/Redo and save/reload. Native checks below used the new Release build
in a fresh empty session after the user confirmed the old app was closed.

- [x] Click blank channel areas: selects track without changing level/pan.
- [x] Drag pan horizontally on the knob; dragging beside it leaves pan unchanged.
- [x] Group fader drag updates both selected channels; numeric entry with Select
      All changes both to the entered value without losing the selection.
- [x] Drag channel name after another channel; arrangement order follows.
      Edit menu Undo restores the original order.
- [x] Duplicate from mixer and arrangement context menus; adjacent copy preserves
      visible level/pan, and Edit menu Undo removes the new copy.
- [x] Resize at the upper mixer boundary; lower switches remain in place.
- [ ] Edge autoscroll, drop-before gesture, small window and collapsed dock.
- [x] User confirmed keyboard Undo/Redo for track reordering.
- [x] User confirmed track copying retains drawn automation, pan and bus assignment.
- [ ] Native save/reopen with audio, plugins and automation (automated coverage
      passes).

## MP3 export follow-up

Automated: mixdown/stems, both MP3 sample rates, all four bitrates, decoded
stereo content and duration, failure/cancellation and existing-file preservation.
Independent FFmpeg decoding confirms encoder delay/padding metadata. See
`tests/test_export_wav.cpp` and the current MP3 entry in `STATUS.md`.

Still a native interaction check: select MP3 in the export dialog, change bitrate,
choose a destination, cancel a long render, close the window/Escape during export,
and listen to a real mixed session including master effects. The user's recording,
mixing and mastering practice test remains separate from the generated-signal tests.

## Other confirmed insert checks

The [19 October test run](RELEASE_FINAL_CHECK_2026-10-19.md) also establishes:

- EQ/compressor chain copied to bus and FX; order and Undo/Redo checked.
- Bus effects disabled together and restored through Undo.
- FX effects removed and restored through Undo with original IDs;
  aux return retained.
- Clipboard appended or explicitly replaced; Undo restores the original state.
- Default preset loaded onto Master and preset file onto an audio channel;
  order, separate instances and session reopening checked.
- Bus Mute and FX Solo correctly change engine state, including Undo.

## Completed functional test round

The following 21 checks were completed between 11 and 15 October.
They are not a total count of automated tests.

| ID | Specific remaining check | Status |
|---|---|---|
| ORD3 | Automatic backups in the Backups folder | Done: three valid backups with audio reference |
| ORD4 | Controlled crash and recovery | Done: dialog, contents and corrected save path checked |
| MED1 | New recording in the named audio folder | Done: microphone recording, WAV and listening confirmation |
| ORD7 | Old flat session and its backups | Done: test copy loaded, waveforms/transport/levels checked; recovery and rotating autosave in the adjacent Backups folder, each with 16 tracks and 14 valid audio references. See TEST_HISTORY.md |
| MED2 | Comp file in the Audio/Comps folder | Done: four-second comp WAV in its own Audio/Comps folder; nine section midpoints compared sample-exactly against selected takes |
| MED3 | Bounce file in the Audio/Bounces folder | Done: “Bounce nur Event” created in the test app; WAV with signal, identical length, valid session reference; Undo/Redo and reopening passed. See TEST_HISTORY.md |
| MED4 | Recording before first save | Done: recording after device change and count-in in the correct unnamed location; first save copies WAV byte-identically to its own audio folder, reopening checked with waveform and active playback. See TEST_HISTORY.md |
| CO1 | Composite waveform in the arrangement | Done: arrangement and dock show the effective comp after loading, switching back and collapsing; save retains the active source |
| CO3 | Waveform changes on take switch | Done: take 1 via lane and return via “Comp anhören” live; Undo/Redo correct. Interaction check confirms take 2 and return to comp through the menu. Comp retained |
| S3 | Save Solo and reopen | Done: Solo set in mixer, written via Cmd+S, original reopened and Solo indicator confirmed |
| S4 | Continue dragging a loaded loop without a position jump | Done: loaded range 0.5–4.5 s moved to 1–5 s, left boundary to 1.5 s, right to 5.5 s; range restored to 0.5–4.5 s. No jump to song start. See TEST_HISTORY.md |
| V18 | Save metronome settings and reopen | Done: metronome on, count-in/pre-roll both 2, recording click off, accent on, volume set to 0.35, saved and confirmed after reopening. See TEST_HISTORY.md |
| T | Play and record an instrument track with the keyboard | Passed 2026-10-14: interaction check confirms playing, recording and playback; 144 notes present. REC, display, double-click, delete/Undo/Redo, moving, extending and saving/reopening confirmed live. Full suite 414 passed/11 skipped. See `TEST_HISTORY.md`. |
| B3 | Synth controls, envelope and polyphony | Passed 2026-10-14: live controls/waveform selection and technical audio acceptance with sample, envelope and four-voice comparisons; 9 synth cases/110 assertions green. No subjective listening test claimed. See `TEST_HISTORY.md`. |
| B4 | Export against playback and a second export | Passed 2026-10-15: first two exports PCM-bit-identical; another export after unmuting, comparison 3. Interaction check confirms identical sound against project playback. See `TEST_HISTORY.md`. |
| B6 | AI target on a track named Master | ✅ Live 2026-10-15: EQ and compressor only on the ordinary track Master; master bus and control unchanged. |
| C1 | Tempo map before a time-offset clip | Passed 2026-10-15: 27 tempo points created live, base tempo before clip 120.2 BPM; 48 onsets at most 5.98 ms from the grid, map and clip position saved. 2 cases/22 assertions green. |
| C2 | Warning on sparse kick track, drum-bus countercheck | Passed 2026-10-15 with synthetic rhythm signals: sparse 45% warning (threshold 60%), dense without warning, 11 tempo points/120.2 BPM. |
| C3 | Specific reason for vocal-tuning rejection | Passed 2026-10-15: non-tonal test clip explicitly reports no detectable pitch live, rather than a generic list of other rejection causes. Automated C3 cases also green. |
| ORD2-Nachprüfung | Save As in an existing folder without double nesting | Done: Save As twice, second time in the existing folder with replacement; valid session file, audio and backups, no double nesting |
| ORD5-Tastenkürzel | Save explicitly via Cmd+S | Done: Cmd+S wired up; initial destination selection including cancellation checked, then Mute/Solo from arrangement/mixer verified in the same file; Cmd+Option+S separate. See TEST_HISTORY.md |

## Not yet accepted

- Public first-time model download through the regular app dialog.
- Physical MIDI keyboard and cue outputs on a multichannel interface.
- Other third-party OOP editors, including sound comparison.
- Insert combinations not yet individually evidenced: moving whole chains with
  Option, all destination channels, reordering and all visual toggle states.
  The chain, preset and bulk checks evidenced above remain passed.
- Third-party presets and sidechain configurations with Undo/Redo in the native UI.
- Complete interaction comparison and localization of all dialogs.

Current automated results are in [STATUS.md](../STATUS.md).
This list neither claims untested checks as passed nor reopens individual checks
that already have supporting evidence.
