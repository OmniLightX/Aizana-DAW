# Change log

## Unreleased — 21 October 2026

These changes are included in the source tree. They do not announce a new
installer, signed app or downloadable binary release.

### Audio devices

- Defer combined CoreAudio input/output restarts to the message thread, avoiding
  synchronous shutdown from device-notification callbacks (including Bluetooth
  device changes). Cancel queued restarts when closing the device.

### Recording

- Draw live audio waveforms in the arrangement using the recording engine’s
  existing thumbnails, without changing input audio or mixer meters.

- Give new non-loop audio recordings playback priority over overlapping clips,
  preserving original audio and playback outside the overlap. Add an undoable
  recording-choice menu; selections persist with the session.

- Mute existing audio clips on the recording track during capture, preserving
  playback before and after punch recording and playback on other tracks.

### KI-Studio

- Fix Codex startup outside a Git checkout, use a dedicated workspace and enable
  automatic approval review so non-interactive MCP calls can be evaluated.

### Mixer and track editing

- Remove bus, FX and cue channels from their mixer context menu. Incoming outputs
  return to master and sends to the removed return are cleared, with Undo/Redo.

- Select channels by clicking empty strip areas or controls. Existing multiple
  selections remain selected when operating their controls.
- Restrict fader interaction to its control and numeric field, and pan interaction
  to the knob. Pan uses horizontal dragging. Empty adjacent areas select tracks.
- Drag regular audio/instrument channels to reorder within their current parent;
  arrangement order follows. Undo/Redo restores order.
- Duplicate tracks from arrangement or mixer context menus. Copies have independent
  clip/plugin/automation identities, preserve output routing and reference the same
  media files. Copies are disarmed and do not reuse the original freeze cache.
- Move lower-dock resizing to the boundary above the mixer/editor while preserving
  bottom view switches.

### Automation

- Add a track-header Automation submenu for show/hide, parameter selection and
  Read/Touch/Latch/Write. Visibility and parameter selection preserve curves and mode.
- Synchronize the open lane's parameter and mode selectors with context-menu changes.
- Correct mode decoding so the first dropdown selection works and menu changes
  do not falsely display Read. Mode changes from the menu have an Undo transaction.

### Export

- Add built-in LAME 3.100 MP3 encoding for stereo mixdown and stems at 44.1/48 kHz
  and 128/192/256/320 kbit/s CBR, including encoder delay/padding metadata.
- Encode on a worker after PCM rendering. Publish destination files only after
  successful completion; cancellation/failure preserves existing destinations.
- Update format/extension and bitrate controls, cancellation/close handling,
  overwrite prompts and stem error handling.
- Bundle upstream encoder source, build integration and license notices.

### Build dependencies

- Remove the unused legacy demucs.cpp library and its separate Eigen download
  from the app and test builds. Stem separation continues to use Mega 53/ONNX.
- Remove obsolete standalone notices; preserve the complete ONNX Runtime notices,
  including its Eigen attribution. Incremental app builds remove old notice folders.

### Documentation and validation

- Update English/German manuals and feature guides, export/distribution notes,
  license notices and live-verification status.
- Final Debug engine suite: 459 passed, 6 skipped, 50,219 assertions.
- New automation UI regression: reproduced six failures before the correction;
  all 21 assertions pass after correction, including Undo/Redo and curve preservation.
- Mixer/track subset: 8 cases and 162 assertions passed in Debug and Release.
- Corrected Release app build and bundle audit pass. Native interaction checks confirmed
  track reorder Undo/Redo, copying with automation/pan/bus assignment and first-click
  automation mode selection in both menu and lane.
- Remaining native combinations and optional hardware checks are recorded in
  [the live checklist](docs/LIVE_TEST_CHECKLIST.md); see [STATUS.md](STATUS.md).

### Name and artwork permissions

- Permit use of the Aizana name, logo and banner in clearly labelled community
  versions, including commercial distributions, with origin and maintainer
  identification and no false implication of official approval.
- Document the terms in English and German and link them from both guides.
