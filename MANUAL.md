**English** | [Deutsch](MANUAL.de.md)

# Aizana – User Manual

For Aizana 0.3.0 Alpha · Updated: October 20, 2026

This manual takes you from your first project through recording and mixing to
export. It describes the current macOS version. The app interface is German;
the instructions retain its actual menu and control labels so you can find them.
Some dialogs and plugin menus already use English labels.

The alpha is being prepared as source code for building yourself. The
[build instructions](README.md#build-from-source) and
[getting-started guide](docs/ALPHA_INSTALL.md) describe the requirements.
The verified target machine is an Apple Silicon Mac running macOS 13.3 or later.
This manual does not replace comprehensive device and plugin validation.
See [STATUS.md](STATUS.md) for the documented verification status.

## Contents

1. [Terminology and interface](#1-terminology-and-interface)
2. [Your first session](#2-your-first-session)
3. [Importing audio](#3-importing-audio)
4. [Recording](#4-recording)
5. [Loop recording and comping](#5-loop-recording-and-comping)
6. [Editing clips](#6-editing-clips)
7. [Mixing and signal paths](#7-mixing-and-signal-paths)
8. [Plugins and insert chains](#8-plugins-and-insert-chains)
9. [MIDI and instruments](#9-midi-and-instruments)
10. [AI setup](#10-ai-setup-for-existing-individual-tracks)
11. [AI mixing and references](#11-ai-mixing-and-references)
12. [Stem separation](#12-separating-a-song-into-stems)
13. [AI Studio and plugin generator](#13-ai-studio-and-plugin-generator)
14. [Export](#14-exporting-a-mix-and-tracks)
15. [Saving and recovery](#15-saving-and-recovery)
16. [Troubleshooting](#16-troubleshooting)
17. [Keyboard and mouse](#17-keyboard-and-mouse)
18. [Licenses and further documentation](#18-licenses-and-further-documentation)

## 1. Terminology and interface

| Term | Meaning |
|---|---|
| Session | Your project, including tracks, edits, routing, and plugin states |
| Track | A place for audio or MIDI clips and their signal path |
| Clip | An audio or MIDI segment in the arrangement |
| Arrangement / Edit | The arrangement of clips over time |
| Mixer / Mix | Channel strips for levels, pan, effects, and routing |
| Insert | An effect or instrument in a channel's signal path |
| Bus | A shared output for several tracks, such as all drums |
| FX channel | An effect channel fed in parallel, for example for shared reverb |
| Send | A portion of a signal additionally sent to an FX channel |
| Master | The mix's shared main output |
| Stem | An individual track or instrument group; not necessarily a single sound |
| Monitoring | Listening to an input signal |

The transport bar at the top contains playback, stop, recording, tempo, metronome,
and loop controls. **Edit** and **Mix** change the focus of the display.
Use **Ansicht → Einsteiger-Welt** (View → Beginner World) to switch the interface
view for the same session. This view does not create a second copy of your song.

Selecting a track and selecting a clip are different: to edit a recording,
select the clip; for input, solo, or record arming, select the track header.
Plugin windows may intercept keyboard input. Click back into the arrangement
before using a DAW keyboard shortcut.

## 2. Your first session

1. Launch the app you built yourself.
2. Open **Datei → Neue Session** (File → New Session).
3. Choose **Datei → Speichern unter…** (File → Save As), name the song, and create
   a dedicated project folder. This gives the session a deliberate save location early on.
4. Open **Aizana → Einstellungen…** (Settings) and check the audio settings.
   Select the device you want to use for listening and, if needed, recording.
5. Import a short audio file for an initial test.
6. Start playback with the space bar. Check that the playhead and level meters
   move and that you can hear the signal at the selected output.
7. Stop and save with **Command-S**.

Under **Song → Audiogerät einrichten…** (Set Up Audio Device), you can map named
input buses to hardware channels. This mapping is distinct from a mix bus:
an input bus describes where a recording comes from; a mix bus combines
existing tracks.

A small test session is enough to get started. Before opening an important
project in a new version, back up the entire session folder.

## 3. Importing audio

### A file or several existing tracks

Drag audio files from Finder into the arrangement, or use
**Datei → Audio importieren…** (File → Import Audio). Multiple files may be placed
on separate tracks. Afterwards, check their positions, track names, and audible content.

If related stems share the same starting point, preserve that shared time
position when importing. A quiet or silent introduction may be part of their
intended synchronization. Do not trim it independently for each file.

### Import with assignment assistance

**Datei → Stems importieren (KI-Setup)…** (File → Import Stems (AI Setup)) analyzes
tracks that are already separated and suggests names, groups, and level adjustments.
You can review the assignments before clicking **Anwenden** (Apply).
Chapter 10 describes the full procedure.

## 4. Recording

### Input and record arming

1. Create a track with **Command-T**.
2. Select the audio input in the track header. Check the actual hardware channel;
   a microphone connected to input 1 does not provide a signal on input 2.
3. If needed, manage input buses through **Song → Audiogerät einrichten…**
   (Set Up Audio Device) or **Manage Buses…** in the input menu. Give them clear names.
4. Select the appropriate mono/stereo track format. The format switch alone
   does not connect a different hardware input.
5. Enable **REC** in the track header. This arms the track for recording.
6. If needed, enable **I** to monitor through Aizana.
7. Check the input level, then start recording in the transport bar.
8. Stop, listen to the recording, and save.

**REC in the track header and recording in the transport are two separate steps.**
macOS may ask for permission the first time the microphone is accessed. If no
recording comes through, also check microphone permission in macOS System Settings.

Monitoring through speakers can cause feedback. Use headphones for microphone
recordings. If your interface already provides direct monitoring, avoid
unintentionally monitoring twice through both the interface and the DAW.

During audio recording, existing clips on the recording track are silent
within the capture region. Input monitoring and other tracks remain audible
when their monitoring and routing are enabled. Existing audio plays before
and after a punch region. This monitoring rule does not delete existing clips.

A subsequent recording pass with looping off takes playback priority over
older clips on the same track within its time range. Existing audio outside
the overlap remains audible; original clips and audio files are preserved.
Right-click an overlapping clip and choose **Aufnahme bevorzugen** to select
an earlier pass. The choice supports Undo/Redo and is saved with the session.
The same menu can resolve existing overlaps in older sessions. Loop takes
continue to use the existing take and comping controls.

During recording, the arrangement displays the incoming audio as a growing
waveform. Its height reflects the recorded signal; mixer meters remain separate.
The waveform follows scrolling, zoom, punch boundaries, and the current loop pass.

### Metronome, count-in, and punch

Enable the metronome in the transport bar. Its menu provides count-in, pre-roll,
and click volume settings. Set the tempo and time signature appropriately
before recording in time.

**Auto-Punch** restricts recording to the region between the loop-in/out markers.
Check these boundaries before starting; an incorrectly set region may explain
an apparently missing recording.

Physical MIDI keyboard tests and separate hardware cue tests have not been
fully signed off for this release. This is distinct from ordinary audio
playback and the documented recording path.

## 5. Loop recording and comping

1. Set the loop-in/out markers around the section you want to record.
2. Enable loop, select the input, and arm the track.
3. Start recording and perform the section over several passes.
4. Stop and open the recorded clip's take view.
5. Select a complete take for comparison, or assemble sections of different
   takes into a comp.
6. **Zusammengesetzt (Comp)** (Assembled (Comp)) returns to the preserved comp
   after you have listened to a complete take.
7. Listen to the transitions and save the session.

A comp is a selection from the takes. Keep the associated audio files.
The session file alone is not enough for sharing or backup.

## 6. Editing clips

### Selecting and moving

Click a clip. **Command-click** adds other clips to the selection or removes
them. Dragging a selection rectangle across an empty area selects clips in that
region. Use the arrow tool to move clips in time or onto another track.
After dropping, check the start position and destination track.

The grid helps with rhythmic placement. **Alt/Option** can temporarily disable
snapping while dragging clips. The same key has a different function when
dragging plugins; see chapter 8.

### Cutting, trimming, and volume

- **Command-E:** split at the playhead.
- **C:** scissors tool; **Escape:** return to the arrow tool.
- Drag the lower half of a clip's left or right edge to trim the visible audio section.
- Fade handles at the upper corners change the fade-in and fade-out.
- The clip gain handle at the top center changes the clip's level.
- Select two overlapping audio clips on the same track and press **X** to create
  a crossfade.
- **Command-Z** undoes an edit.

Trimming moves the clip's playback boundaries; it does not delete the original
audio material. Listen to edit points after cuts and fades, especially with
vocals, low notes, and long reverb tails.

### Tempo and advanced editing

The transport tempo and time signature define the musical grid.
**Tempo tippen** (Tap Tempo) determines a tempo from several clicks in time.
Tempo changes, audio stretching, and warp editing are different operations:
changing the grid alone does not guarantee that every imported file will
follow it musically.

Advanced audio editing includes Bounce in Place, transient/warp tools, and
offline pitch correction. Start with a copy of the session and compare the
result with the original. The specific actions available depend on the clip
type and selection.

## 7. Mixing and signal paths

### Levels and pan

Open **Mix** or press **Command-M**. Each channel provides level, pan, mute/solo,
and inserts. The master is on the right. Start by establishing an audible
volume balance before adding many effects.

**Mute** silences a track; **Solo** helps you listen selectively. Exit solo to
check the entire mix. Multiple selected tracks can be switched together.
Check the selection before pressing **M** or **S**.

### Selecting, moving and copying channels

Click anywhere in a channel strip to select its track. Empty space beside the
fader and pan does not change their values. Adjust level directly on the fader
or its scale; drag the pan knob horizontally. Clicking a control in an existing
multiple selection preserves that selection for grouped adjustments.

Drag the channel name or empty strip background onto another audio channel.
The insertion marker shows whether the track will be placed before or after it.
The arrangement follows the new order. Tracks remain in their existing folder;
bus, FX, VCA and cue channels are excluded from this gesture.

Choose **Spur duplizieren** (Duplicate track) from the track header or mixer
channel context menu to create an adjacent copy with clips, effects, settings
and automation. Audio files are shared references; clips, effects and automation
can be edited independently. The copy is not record-armed and does not reuse
the original freeze cache. Duplication and reordering support Undo/Redo.
**Command-D** continues to duplicate clips.

Drag the boundary between the arrangement and mixer up or down to resize it.
View switches remain in the bottom bar.

### Buses and shared reverb

Right-click the name or empty background of a bus, FX or cue channel and choose
**Kanal entfernen** (Remove channel) to delete it and its inserts. Tracks routed
to it return to the master; sends to its FX/cue return are removed. Undo restores
the channel and affected connections together.

To process several tracks together:

1. Create a bus through the track context menu (**Create Bus from Track**).
2. Select the desired bus as the output of the tracks you want to combine.
3. Add shared effects to the bus and check its path to the master.

For parallel reverb:

1. Create an FX channel (**Create FX Channel**).
2. Add a reverb to it.
3. Feed the FX channel using sends from the desired tracks.
4. Adjust the amount of reverb with the sends and the FX channel level.

A bus typically replaces a track's direct output. A send adds a parallel path.
Avoid unintended double routing. A sidechain, by contrast, lets another track
control an effect; that control signal is not the same as an audible send.

### Automation

Open the track's automation view with the **A** button or right-click its
header and choose **Automation → Einblenden** (Show). The same menu hides it.
**Parameter wählen** (Choose parameter) lists volume, pan and available effect
parameters; choosing one opens its curve. Showing, hiding and choosing a
parameter do not alter curve data or the automation mode.
Points or lines describe its changes over time. Both the Automation submenu
and the automation lane provide **Read**, **Touch**, **Latch**, and **Write** modes:

- **Read:** play back existing automation.
- **Touch:** write automation while operating a control.
- **Latch:** continue writing the value after operating a control.
- **Write:** overwrite automation while writing.

Test modes that write automation in a copy first. Then switch back to **Read**
and check the automation.

## 8. Plugins and insert chains

### Finding and opening plugins

Aizana includes its own instruments and effects. Install third-party VST3, AU,
and CLAP plugins separately. Afterwards, run
**Ansicht → Nach Plug-Ins suchen…** (View → Scan for Plugins).

Click **+** in the insert header to add a plugin. Double-click an entry to open
its editor. A single click selects the entry; the selection is indicated by
color. The dot on the right of the entry switches the effect between active
and bypassed.

### Copying and moving

| Gesture | Result |
|---|---|
| Drag an individual plugin entry to another channel | Copy the plugin with its settings; the original remains |
| Hold **Alt/Option (⌥)** when dropping on the other channel | Move the plugin; the original leaves the source channel |
| Drag the insert title to another channel | Copy the entire effect chain |
| Hold **Alt/Option** when dropping the chain | Move the chain |
| Drag a plugin within the same list | Change the order |

The Option key is next to Command on a Mac keyboard and may be labeled **⌥**,
**option**, or **alt**. When dragging between channels, its state at the moment
of dropping is what matters.

To drag a single plugin, grab its entry, not the insert title. Copying adds to
the destination chain. **Ersetzen** (Replace) is a separate menu action.
Use the arrow menu to copy, paste, save, and load chains. Chain presets use
the `.aizfxchain` extension.

Copies retain settings, order, and bypass state, but not song automation or
external sidechain source references. Check sidechain and automation separately
after copying. Moving preserves the instance and its connections.

### Out-of-process plugin windows

With optional hosting in a separate process, Aizana opens the native plugin
editor directly. No additional notification window is needed. Closing and
reopening should reuse the same plugin instance. Removing the plugin or
switching sessions closes its associated windows. Native out-of-process editor
windows cannot be docked.

This workflow has been verified with Fresh Air. Other manufacturers and formats
may behave differently. A separate process is not a guarantee against every
plugin failure.

## 9. MIDI and instruments

A MIDI track contains notes and control data, not a finished audio recording.
It needs an instrument to make it audible.

1. Add an instrument to a suitable track, such as **Synth (Poly)** or
   **Drums (Drum Machine)** from the instrument menu.
2. Double-click a MIDI clip to open it in the piano roll.
3. Draw notes and adjust their positions and lengths.
4. Edit note velocities in the velocity lane.
5. Use quantization and swing as needed, and listen to their effect.

You can also show the controller lane. The synth offers two oscillators and
presets. The drum instrument has an 8×16 sequencer and can use custom WAV samples.

**Song → Tastatur spielen (Musical Typing)** opens input through the computer
keyboard. This does not replace validation of an external MIDI keyboard.
For an external device, its MIDI input must be selected on the track.

## 10. AI setup for existing individual tracks

This tool is suited to a folder of recordings that are already separated,
for example bass, drums, guitars, and vocals.

The following preview appears in **Pro-Modus** (Pro mode). In **Einsteiger-Welt**
(Beginner World), the plan is applied directly and can be undone. If you want
to assign the groups yourself beforehand, first disable Beginner World through
the **Ansicht** (View) menu.

1. Choose **Datei → Stems importieren (KI-Setup)…** (File → Import Stems (AI Setup)).
2. Select the related audio files.
3. Wait for analysis.
4. In **KI-Setup — erkannte Stems** (AI Setup — Detected Stems), check the file
   names, suggested track names, groups, and gain values.
5. Correct incorrect assignments, for example if an effect was identified as
   vocals because of its file name.
6. Click **Anwenden** (Apply) and listen to the configured session.

File names and audio analysis provide clues, not reliable instrument recognition.
AI setup requires neither the 355 MB stem model nor access to Claude or Codex.
It does not separate a mixed file.

## 11. AI mixing and references

**KI-Mix** (AI Mix) in the transport bar, or **Song → KI-Mix (Heuristik)…**
(AI Mix (Heuristic)), creates a rule-based mixing suggestion. No bundled trained
mixing model is required.

1. Save your starting session.
2. Check tracks, groups, and routing.
3. Optionally select a genre profile under **Song → KI-Referenz** (AI Reference),
   or analyze a reference file that you have permission to use for this purpose.
4. Start KI-Mix. In beginner mode, the suggestion is applied immediately;
   in Pro mode, a preview is available.
5. Listen to the result in context and compare it with the starting point.
6. Adjust individual decisions or use Undo.

A reference provides target values and guidance. A displayed LUFS value is not
automatically a mandatory loudness target. AI mixing replaces neither checking
the export nor your own decisions about sound.

## 12. Separating a song into stems

This separates a **mixed audio file** into instrument groups.
It is a different operation from importing existing stems.

1. Choose **Song → Song in Stems trennen (KI)…** (Separate Song into Stems (AI)).
2. Select the source file.
3. If the model is not yet installed, Aizana offers a one-time download of
   approximately **355 MB**. Confirm only if you want to use the feature.
4. After download and verification, choose:
   - **6 Spuren — mit Gitarre und Piano** (6 Tracks — Including Guitar and Piano):
     drums, bass, vocals, guitar, piano, and other.
   - **4 Spuren — Gitarre und Piano in Übriges** (4 Tracks — Guitar and Piano in Other):
     drums, bass, vocals, and other.
5. Wait for local processing. The progress display offers cancellation.
6. In Pro mode, review the subsequent assignments in AI setup and apply the
   result. In Beginner World, it is applied immediately.
7. Listen to each output individually and in context.

Both output options use the same MVSep-Mega-53 model version. Two separate
models are not required. “Übriges” (Other) is derived from the residual signal.
The drums output does **not** additionally separate kick, snare, and hi-hat
into individual files.

Outputs below the volume threshold are skipped during import and reported.
Consequently, six audible tracks will not always appear in the session, for
example when an instrument group is absent from the source.

Processing is local; your audio is not uploaded for this separation.
The model download requires internet access and is checked for size and
checksum. Python is not required to use the integrated separation.

**Limitations:** Bleed, missing components, and artifacts can occur.
Synthetic keyboard timbres in particular may separate less well than acoustic
piano. Keep the original mix. If the build was created without ONNX Runtime,
this feature is unavailable.

**Download verification status:** The complete model transfer and checksum
have been verified. The anonymous first download through the app dialog has
not yet been signed off; see [STATUS.md](STATUS.md) for the current status.

Provenance, licensing, and verification limits are documented in the
[model report](docs/OPEN_STEM_RELEASE.md).

## 13. AI Studio and plugin generator

**Song → KI-Studio (Chat: Plugins bauen lassen)…** (AI Studio (Chat: Have Plugins
Built)) opens the additional agent interface. It can use locally installed
Claude Code or Codex tools. Installation, sign-in, costs, and data transfers
are governed by the respective provider. These services are not included in
the free Aizana license.

Choose **Codex** in the **KI** dropdown after installing and signing in to its CLI.
Use **Neu verbinden** to detect newly installed tools. Codex tool requests undergo
automatic approval review; the supported CLI and setup are described in
[docs/MCP.md](docs/MCP.md).

**Song → Plugin generieren…** (Generate Plugin) and **Aizana-Plugins verwalten…**
(Manage Aizana Plugins) belong to the integrated generator. Creating new plugins
requires Faust and Apple Command Line Tools on your computer. These tools are
not shipped with the app. Test generated plugins in a test session first.

Generated plugins execute native code with your user permissions. Aizana loads
and initializes them while scanning, before insertion on a track. Only generate
plugins from trusted sources; using a test session does not sandbox their code.

Setup and technical interfaces are described in [docs/MCP.md](docs/MCP.md).
You do not need to set up AI Studio for ordinary recording, editing, and mixing.

## 14. Exporting a mix and tracks

1. Save the session and check mute/solo and the output to the master.
2. Choose **Export → Mix exportieren…** (Export Mix) for the full mix, or
   **Export → Stems exportieren…** (Export Stems) for individual tracks.
3. Select **WAV**, **AIFF**, or **MP3**, the sample rate, and bit depth or MP3 bitrate.
4. Select a destination file or folder.
5. Start the export and wait for the completion message.
6. Open the resulting file in an audio player and check its beginning, end,
   level, and completeness.

WAV/AIFF support 44.1 / 48 / 96 kHz and 16 / 24 / 32 bit.
MP3 supports 44.1 / 48 kHz and 128 / 192 / 256 / 320 kbit/s constant bitrate
(default: 320). LAME is included; no separate installation is needed. Choosing
MP3 with 96 kHz selected switches to 48 kHz. The file extension follows the format.

**Abbrechen** cancels the current export. Closing the export window also cancels.
A failed or cancelled file does not replace an existing destination. For stems,
files completed before cancellation remain in the destination folder; an error
stops the remaining stems. Duplicate track names receive numbered suffixes.

Mixdown includes master effects. Before rendering, check that the limiter or
other master inserts are configured as intended. Exporting stems renders
existing tracks; it does not perform AI separation of a finished mix.

## 15. Saving and recovery

**Command-S** saves the session. On the first save, choose a name and location.
Use **Datei → Speichern unter…** (File → Save As) to save to a different destination.
To open a session, use **Datei → Session öffnen…** (File → Open Session) and select
the `.tracktion` file.

A project also includes its media. Typical structure:

```text
My Song/
    My Song.tracktion
    My Song Audio/
        Comps/
        Bounces/
    Backups/
```

Back up the entire folder. Plugins you installed yourself may also require
external sample libraries or vendor files; a session file does not replace them.

Autosave and backups supplement deliberate saving. If Aizana offers recovery,
check the recovered state before discarding it. If a save error appears, check
write permissions, free disk space, and the availability of external drives.
Do not assume that the latest change has already been saved.

## 16. Troubleshooting

| Problem | First check |
|---|---|
| No sound | Is the transport running? Correct audio output? Mute/solo? Track routed to the bus/master? Master level? |
| No input signal | Check the hardware channel, input bus, interface, and macOS microphone permission |
| No recording | Check track REC and transport recording separately; check punch boundaries |
| Doubled or delayed monitoring | Compare the interface's direct monitoring with DAW monitoring |
| Missing plugin | Is a supported format installed? Scan for plugins again afterwards |
| Plugin moved instead of copied | Release Alt/Option when dropping if you want a copy |
| Entire insert chain was dragged | Grab the individual plugin entry instead of the insert title |
| Shortcut does not respond | Leave the text field or plugin editor and focus the appropriate DAW view |
| Incorrect AI setup group | Correct the suggestion in the group selector before applying |
| Stem download fails | Check internet access and public availability of the model release; see chapter 12 |
| Stem result sounds poor | Keep the original; listen for bleed and model limitations |
| MP3 export fails | Choose 44.1 or 48 kHz; check the destination and available disk space, then read the export error |
| Session reports missing media | Check the complete project folder or backup and mounted drives |
| App or plugin crashes | Back up the starting session; narrow down the error with a small test session and a specific plugin |

A useful bug report includes the version/commit, macOS version, audio device,
plugin format and version if applicable, and reproducible steps. Describe the
expected and actual behavior. Check attachments for private paths, credentials,
and personal information. Publish only audio material you have permission to share.

## 17. Keyboard and mouse

| Command | Action |
|---|---|
| Space bar | Start/stop playback |
| Command-S | Save |
| Command-Z / Command-Shift-Z | Undo / redo |
| Command-T | Add track |
| Command-M | Toggle mixer |
| Command-E | Split clips at the playhead |
| Command-C / Command-V / Command-D | Copy / paste / duplicate clips |
| Command-click | Add to or remove from the selection |
| M / S | Mute / solo according to the active selection |
| R | Arm the active track for recording |
| C / Escape | Scissors / arrow tool |
| F | Automatically follow the playhead |
| X | Crossfade two overlapping audio clips on the same track |
| Double-click a MIDI clip | Open the piano roll |
| Double-click a plugin entry | Open the plugin editor |
| Alt/Option when dropping inserts between channels | Move instead of copy |

These commands depend on focus and selection. While entering text, letters
should edit the text rather than trigger mute/solo or recording.

## 18. Licenses and further documentation

Community versions may use the Aizana name and logo under the
[name and logo permissions](docs/BRANDING.md). Modified versions must clearly
identify their origin and maintainers without implying official approval.

MP3 export uses **LAME 3.100**, an open-source encoder under
**LGPL-2.0-or-later**. Original source, license texts and redistribution details
are documented in the [licensing overview](docs/OPEN_SOURCE_LICENSING.md#mp3-encoder-attribution).


Using Aizana does not automatically place the music you create under the
software license. You must separately observe the rights associated with any
samples, recordings, and third-party plugins you use.

Aizana as a whole is not MIT-licensed. Its own code uses GPL-3.0-or-later;
JUCE, among other components, uses the AGPLv3 licensing route here. The stem
model has a separately documented MIT licensing route.

- [Project overview and build](README.md)
- [Licensing model and redistribution](docs/OPEN_SOURCE_LICENSING.md)
- [Project license](LICENSE.md)
- [Third-party components and attribution](THIRD_PARTY_NOTICES.md)
- [Distribution and source obligations](docs/DISTRIBUTION.md)
- [Model provenance and separation limits](docs/OPEN_STEM_RELEASE.md)
- [Current verification status](STATUS.md)
