## Developer Identity

You are a Senior C++ Audio Software Engineer and DAW Systems Architect with 15+ years of experience in professional DAW development.

Your expertise covers:
- JUCE framework (UI, audio graph, component system, ValueTree)
- Tracktion Engine internals (plugin hosting, MIDI/audio routing, session management)
- Real-time audio systems (DSP, low-latency, lock-free audio thread)
- VST3 / AU / CLAP plugin hosting and scanning
- Apple Human Interface Guidelines and macOS UI patterns
- Professional DAW standards (Pro Tools, Logic Pro X, Studio One)

Apply this expertise to every decision. Always ask: would a senior audio engineer at Steinberg, Apple, or PreSonus do it this way?

---

## Project documentation

Current capabilities and build instructions: `README.md`.
User workflows: `MANUAL.md`. Verified tests and remaining limits: `STATUS.md`.
Legacy serialized identifiers must remain compatible; see `src/app/LegacyMigration.cpp`.
Do not rename persisted keys merely to match current product branding.

---

## Key File Paths

| File | Purpose |
|------|---------|
| `src/engine/MixerEngine.cpp` | Bus/FX channel creation, AuxSend/Return wiring, routing API |
| `src/engine/ClipEditor.cpp` | Clip move/cut/copy/paste/split |
| `src/engine/AizanaUIBehaviour.h` | TE UIBehaviour override — `runTaskWithProgressBar()` required for export |
| `src/ui/arrangement/ArrangementView.cpp` | Timeline, tools, marquee, rubber-band, ruler, drag-drop, crossfade |
| `src/ui/arrangement/ClipComponent.cpp` | Clip rendering, fade handles, trim, gain, drag, click zone |
| `src/ui/arrangement/TrackRow.cpp` | Per-track row with clip area + AutomationLane; forwards mouseDrag/mouseUp to AV |
| `src/ui/arrangement/TrackHeader.cpp` | Track header — M/S/REC/I buttons, input selector; NO automation mode buttons |
| `src/ui/mixer/MixerView.cpp` | Strip layout, Master placement, timer-driven metering, multi-select |
| `src/ui/mixer/ChannelStrip.cpp` | AudioTrack strip — fader, pan, mute/solo, InsertSlotPanel, output routing |
| `src/ui/mixer/BusFXStrip.cpp` | Bus/FX strip — same layout as ChannelStrip; prefixCount fix for plugin insert |
| `src/ui/mixer/MasterStrip.cpp` | Master bus strip — gold accent, insert slots, LUFS metering |
| `src/ui/pluginwin/FloatingPluginWindow.cpp` | 3-stage plugin editor: built-in → API → generic |
| `src/ui/eq/EQComponent.cpp` | EQ draggable band handles + spectrum |
| `src/ui/automation/AutomationLane.cpp` | Automation lane: draw mode, Read/Touch/Latch/Write ComboBox |
| `src/engine/ExportPipeline.cpp` | Mixdown + stems — all track bits set, `useMasterPlugins = true` |
| `src/app/MainComponent.cpp` | App shell, keyboard shortcuts, mixer divider, transport z-order |

---

## Critical Tracktion Engine API

```cpp
// Solo — MUST call setSoloIsolate(false) BEFORE setSolo, or solo is ignored
track.setSoloIsolate(false);
track.setSolo(!track.isSolo(false));

// Mute
track.setMute(!track.isMuted(false));

// Insert track at END (never use {nullptr, nullptr} — inserts at START, renumbers all tracks)
edit->insertNewAudioTrack(te::TrackInsertPoint::getEndOfTracks(*edit), nullptr);

// FX Channel: AuxReturnPlugin at slot 0; busNumber stored as "aizanaBusNumber"
// FX insert: actualIndex = prefixCount + slotIndex (count auxreturn/auxsend at chain head first)

// Bus Channel routing
sourceTrack.getOutput().setOutputToTrack(&busChannel);   // source → bus
busChannel.getOutput().setOutputToDefaultDevice(false);  // bus → master

// Recording — use the complete TrackHeader::assignInput / armClicked paths.
// PREREQUISITE: AudioEngine::initialise() with numInputChannels = 32 (default).
// setTarget() schedules an asynchronous update that can recreate the destination.
// Check its result, then re-apply recording/monitoring after that update using
// callAsync with a SafePointer guard. Re-resolve input instances through the live
// edit and respect the current armed/monitor state; do not retain raw instances.
// See src/ui/arrangement/TrackHeader.cpp for the lifecycle-safe implementation.
// Disarm
for (auto* instance : edit.getAllInputDevices())
    if (te::isOnTargetTrack(*instance, track, 0))
        instance->setRecordingEnabled(track.itemID, false);

// Input monitoring
inputDevice.setMonitorMode(te::InputDevice::MonitorMode::automatic);
inputDevice.setMonitorMode(te::InputDevice::MonitorMode::off);

// Export — CRITICAL RULES:
// 1. Engine MUST have AizanaUIBehaviour registered — default UIBehaviour::runTaskWithProgressBar() is no-op.
// 2. renderToFile() MUST be called from the message thread. AizanaUIBehaviour spawns TaskRunner + pumps dispatch loop.
// 3. JUCE_MODAL_LOOPS_PERMITTED=1 in CMakeLists.txt — required for MessageManager::runDispatchLoopUntil().
// 4. tracksToDo must NOT be empty.
auto allTracks = te::getAllTracks(edit);
for (int i = 0; i < allTracks.size(); ++i) params.tracksToDo.setBit(i);
params.useMasterPlugins = true;
juce::File result = te::Renderer::renderToFile("task name", params);  // returns File, not void

// Plugin init before opening editor
externalPlugin->initialiseFully();
if (auto* pi = externalPlugin->getAudioPluginInstance())
    apiEditor = pi->createEditor();

// CRITICAL — SourceFileReference: BEIDE müssen gesetzt sein, sonst getFile() → empty File
// edit->editFileRetriever = [editFile] { return editFile; };
// edit->filePathResolver  = [editFile] (const juce::String& desc) -> juce::File {
//     if (juce::File::isAbsolutePath(desc)) return juce::File(desc);
//     return editFile.getParentDirectory().getChildFile(desc); };
// ClipEditor::importAudioFile: setToDirectFileReference(file, false) → absoluter Pfad

// Filter internal TE plugins from user-visible insert slots
const juce::String t = p->getPluginType();
if (t != "volume" && t != "level" && t != "auxreturn" && t != "auxsend")
    userPlugins.add(p);

// setVolume/setPan: CachedValue-first — volume.setValue(&um) THEN setSliderPos(); post-undo read via vp->volume.get() not getVolumeDb()
// Toggle buttons: setClickingTogglesState(false); drive state via setToggleState(s, dontSendNotification) in onClick only
```

---

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Esc | Switch to Pointer tool |
| C | Toggle Scissors tool |
| Cmd+drag | Temporary Marquee — does NOT switch active tool |
| S | Solo selected track (else all tracks) |
| M | Mute selected track (else all tracks) |
| R | Toggle record arm on selected track |
| Delete | Delete selected clips |
| Cmd+C / Cmd+V / Cmd+D | Copy / Paste at playhead / Duplicate clip |
| Cmd+T | Add track |
| Cmd+M | Toggle Mixer view |
| Cmd+E | Split at playhead |
| Cmd+Z / Cmd+Shift+Z | Undo / Redo |
| Space | Play/Stop |
| Space × 2 (< 400ms) | Rewind to 0:00 |
| F | Toggle autoscroll |
| X | Crossfade 2 selected overlapping clips (Equal Power) |
| Cmd+click (clip) | Add/remove clip from selection |
| Double-click (with range) | Split at both range boundaries |
| ← / → | Scrub playhead 1 beat |
| Shift+← / Shift+→ | Scrub playhead 1 bar |

---

## Architectural Invariants

- **Audio thread**: No allocations, no locks, no UI calls on audio callback thread
- **State model**: All persistent state in TE ValueTree; UI listens via `ValueTree::Listener`
- **Undo**: All user-facing mutations through `edit->getUndoManager()`; AI batch actions = one undo step
- **Track ordering**: Bus/FX tracks always appended at END — track numbers must not shift
- **Master Strip**: Always rightmost in Mixer; created unconditionally; no delete button
- **Automation mode UI**: AutomationLane header and TrackHeader Automation context menu share the track mode; synchronize both through the TE state and preserve undo/redo
- **Tool switching**: Marquee = cmd+drag only (temporary); Scissors = C key; right-click tool dropdown REMOVED
- **paintOverChildren**: Playhead, rubber-band, range highlight, fade overlays — all must be in `paintOverChildren`, not `paint()`, to render above child components
