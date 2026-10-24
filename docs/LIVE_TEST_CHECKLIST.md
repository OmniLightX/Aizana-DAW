# Native interaction checklist

Recorded checks through 24 October 2026. Checked items were observed in native
interaction or confirmed during user testing; unchecked items remain outstanding even if engine tests cover
related behavior. Use disposable sessions and test files.

## Mixer and track actions

- [x] Empty channel areas select without changing gain or pan.
- [x] Pan responds to horizontal dragging on its knob, not adjacent empty space.
- [x] Group fader dragging and numeric entry preserve multiple selection.
- [x] Reordering updates arrangement order; menu and keyboard Undo/Redo work.
- [x] Both track-duplication menus create adjacent copies; Undo removes the copy.
- [x] Copied tracks retain drawn automation, pan and bus assignment.
- [x] Upper mixer boundary resizes the dock; lower switches remain available.
- [ ] Edge autoscroll and drop-before gesture with many tracks.
- [ ] Small window and collapsed-dock resizing.
- [ ] Save/reopen a copied track containing audio, effects and automation together.

## Automation

- [x] A button opens the automation lane.
- [x] Mode changes from the menu and lane work on the first selection.
- [ ] Menu show/hide preserves mode and existing curves.
- [ ] Pan/plugin parameter selection updates both curve and selector.
- [ ] Mode Undo/Redo updates menu tick and lane selector together.

These remaining combinations have related automated regression coverage;
they are not marked as manually passed on that basis.

## Inserts and plugin windows

- [x] Single EQ Option-move between audio tracks; state and Undo/Redo retained.
- [x] Single EQ copy to bus; source retained and destination instance independent.
- [x] EQ/compressor chain copy to bus and FX; order and Undo/Redo retained.
- [x] Bus bulk disable/Undo and FX bulk removal/Undo preserve routing components.
- [x] Clipboard append and explicit replacement; Undo restores prior state.
- [x] Default preset onto Master and preset file onto audio track; order and
      independent instances retained after reopening.
- [x] Bus Mute and FX Solo update engine state with Undo.
- [x] Fresh Air OOP uses one operable native editor; close/reopen and session
      changes terminate the previous worker correctly.
- [ ] Whole-chain Option-move and every source/destination channel combination.
- [ ] All insert selection, drag-preview, reorder and toggle visual states.
- [ ] Third-party presets and sidechain configurations with native Undo/Redo.
- [ ] Other OOP editors and listening comparisons.

## Sessions, editing and instruments

- [x] Create tracks, Undo/Redo, save, reopen and switch sessions.
- [x] Cancel or fail Save/Save As before New Session without discarding edits.
- [x] Failed save preserves existing destination; retry succeeds after restoring permissions.
- [x] Autosave, recovery, legacy flat sessions and Save As without duplicate nesting.
- [x] First-save media collection, comp and bounce files in the session media folders.
- [x] Comp take switching, effective waveform, Undo/Redo and reopening.
- [x] Loaded loop boundaries move without jumping to song start.
- [x] Metronome/count-in settings retained after reopening.
- [x] Computer-keyboard MIDI playing, recording, playback and note editing.
- [x] Synth controls/waveform selection; technical envelope/polyphony tests are separate.
- [x] Tempo analysis with dense/sparse synthetic signals and rejection of non-tonal tuning input.
- [ ] Physical MIDI keyboard and multichannel-interface cue outputs.
- [ ] Complete dialog interaction and localization review.

## Export and stems

- [x] WAV export compared with repeat export and project playback.
- [x] Stem preview/import and Undo/Redo.
- [x] Stem cancellation and session switching prevent late import.
- [ ] MP3 dialog format/bitrate selection, cancellation, Escape/window close and
      listening to a real mixed session including master effects.
- [x] Full anonymous model download from the app URL: size and SHA-256 verified (24 October 2026).
- [x] First model download through the app dialog (24 October 2026): existing model temporarily backed up, download consent accepted, new installed file verified by size and SHA-256.
- [ ] First launch on an independent Mac.

See [test status](../STATUS.md), [test reproduction](TESTING.md),
[MP3 checks](MP3_EXPORT.md) and [model evidence](OPEN_STEM_RELEASE.md).

### Bus/FX channel removal

Pending live verification in the rebuilt app:
- Right-click a bus/FX/cue name or empty strip area and choose **Kanal entfernen**.
- Check that plugin windows close, source outputs return to master and sends to
  the removed return disappear; unrelated bus/FX routes must remain unchanged.
- Undo and redo; check the restored inserts and connections, then save/reopen.

### Recording monitoring

- [ ] Record over an existing clip: hear the input, not the old clip; other
      tracks continue playing. Stop and verify recorded audio.
- [ ] Punch recording: hear existing audio before and after the punch region.
- [ ] Loop recording: old clips remain silent across loops; takes and comping
      remain available after stopping.

### Successive recording playback

- [x] Record, stop, rewind and record again without looping: hear only the new
      pass in the overlap during subsequent playback.
- [ ] Record a shorter replacement inside a longer take: check the original
      before and after it, plus the transitions for clicks.
- [ ] Right-click an overlapping clip, choose **Aufnahme bevorzugen**, switch
      back, Undo/Redo, save and reopen. Confirm all source audio remains intact.

### Live recording waveform

- [x] Live waveform appears during recording (confirmed in user testing on 24 October 2026).
- [ ] Record quiet and loud passages: waveform grows before Stop and reflects input amplitude; existing mixer meters behave unchanged.
- [ ] Scroll and zoom while recording; waveform stays aligned with the timeline and does not cover headers.
- [ ] Check stereo and simultaneous armed tracks, silence, pre-roll, punch-in/out, and several loop passes.
- [ ] Stop and record again: completed waveform remains and the new live waveform belongs only to the active recording.

### CoreAudio device switching

- [x] Two output round trips between built-in speakers and AirPods Pro with built-in microphone input while stopped, in the corrected Release build.
- [ ] Repeat the output switches during playback.
- [ ] Disconnect/reconnect the Bluetooth device; check app responsiveness, audio restart, and reported sample rate.
- [ ] Close the device/settings during a pending change; verify no unexpected restart or crash.
- [ ] Repeat with the audio interface and check recording/monitoring after switching.
