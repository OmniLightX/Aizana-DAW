# In-app mouse control for GUI test runs

**Summary:** Aizana can be controlled through an unnamed pipe in a dedicated
test binary: clicking, dragging, typing and file drops. The distributed app
**cannot** do this: that code is not compiled into it.

---

## Why this exists

The live-test checklist includes checks that require a real drag gesture:
creating fades, drawing clip-gain curves, moving warp markers and dragging faders.
Accessibility (`scripts/gui-test.sh`) exposes clicks and names, but not dragging.

## Process boundary

The controller addresses only JUCE components in the test process. It does not
send system-wide mouse events to other applications.

## How it works

The hook calls `juce::ComponentPeer::handleMouseEvent`, the same function macOS
calls for a real click (`juce_NSViewComponentPeer_mac.mm`, `sendMouseEvent`).
This runs JUCE hit testing, the actual handlers and the actual
press–move–release sequence used in normal interaction.

The signature carries **no target**: point, modifiers, pressure, time. The target
is the object on which the method is called, and `ComponentPeer::getPeer()`
enumerates only windows belonging to **this** process. No parameter anywhere in
the chain can name a foreign process; the event never reaches the screen.

The control channel is an **unnamed pipe on stdin**: no path, port or entry in
any namespace, writable only by the launching process and terminating with it.
A socket or watched file would be addressable, creating an entry point.

## Safeguards

1. **Compile time** — without `-DAIZANA_UITEST_HOOK=ON`, `src/testing/UiTestHook.cpp`
   is not compiled at all; the header exposes only a `constexpr false`.
   The instructions do not exist in the distributed binary.
2. **Runtime** — even in the test build, requires both `AIZANA_UITEST=1` **and**
   stdin connected to a pipe.
3. **Prohibited facilities** — the translation unit references neither CGEvent/CGWarp
   nor accessibility, sockets, child processes or networking (checked in the symbol table).
4. **Test mode reduces the app** — when the hook is active, the AI bridge's
   loopback server, AI Studio CLI launch and model download remain disabled.
   So do the splash screen and microphone prompt (native dialogs that block startup).
5. **Modifier guard** — the combination `ctrl+alt+cmd` is rejected. Otherwise,
   `Slider::isAbsoluteDragMode` enters velocity mode, which moves the **real**
   mouse pointer on release via `MouseInputSource::setRawMousePosition` →
   `CGWarpMouseCursorPosition`. Every gesture also aborts as soon as
   `isUnboundedMouseMovementEnabled()` reports true.

Demonstrated by **failed attacks**, not assurances:

```bash
./scripts/uitest-guard-check.sh
```

The checks include counterchecks that trigger the respective failure case.

## Prerequisites and command behavior

- The screen must be unlocked; `uitest.py` checks this before launch.
  An `OK` response confirms dispatch only, not a visible effect.
- Keyboard modifiers are set during dispatch and restored afterward.
- Target lookup respects the visible bounds of all ancestors.
  Hidden or scrolled-out elements return an error.
- Use an audible section of the test material for waveform checks.
- Establish the view mode and dock state before each run. `wennfehlt` allows
  state-dependent steps.
- Native menu commands are invoked with `menu <top> <itemId>`.
- `taste backspace` corresponds to the MacBook backward-delete key;
  `taste delete` deletes forward.

## Limitations (remain with the human tester)

- **PopupMenus do not respond to synthetic clicks**, either top-level entries
  or submenus. `MenuWindow::MouseSourceState::timerCallback` and `isOver()` query
  `source.getScreenPosition()`, which follows `getRawScreenPosition()` to
  `MouseInputSource::getCurrentRawMousePosition()`: the **real** system mouse,
  which the hook never moves (and must not move under safeguard 5). Observed:
  clicking “Duplicate” had no effect; a submenu opened neither on click nor hover.
  **Use `press <Name>` for this**: it invokes the entry directly on the model via
  `AccessibilityActionType::press` (`showSubMenuFor` for submenus), an in-process
  method call like `menu <top> <itemId>`. No AXUIElement or accessibility permission.
  This makes context menus fully operable; entries are addressed by their
  **text**, which appears only in the accessibility title (see `describe()`).
- **Native menu bar** — on macOS this is an NSMenu (`MainWindow.cpp`,
  `setMacMainMenu`), fundamentally unreachable by these mouse events. The hook
  therefore invokes menu items directly on the `MenuBarModel`
  (`menu <top> <itemId>`), an in-process method call.
- **Native dialogs** — the hook can *open* `NativeMessageBox` and `FileChooser`
  (NSOpenPanel), but cannot operate them; the message thread blocks and commands
  time out. Fallback: `scripts/gui-test.sh` (accessibility; addresses elements
  by the **name of a named app**, without broadcasting events).
- **Third-party plugin interfaces** — embedded NSViews or a separate process
  (OOP). They receive events from AppKit, not JUCE.
- **The macOS→JUCE path itself** is not tested. A defect in that path would only
  be noticed by a human tester.

## Usage

```bash
cmake -S . -B build-uitest -DCMAKE_BUILD_TYPE=Release -DAIZANA_UITEST_HOOK=ON
cmake --build build-uitest --target Aizana -j 10

./scripts/uitest.sh lauf drehbuch.txt      # Run a test script
./scripts/uitest.sh lauf -                 # Read the script from stdin
```

For editor test scripts 01–03, supply your own WAV file that you are authorized
to use. The scripts do not use a bundled music recording:

```bash
export AIZANA_TESTWAV="/path/to/test.wav"
./scripts/uitest.sh lauf scripts/uitest-drehbuecher/01-editor-vorbereiten.txt
```

Script commands: `klick` · `dklick` · `ziehe <Name> <dx> <dy>` ·
`ziehevon <A> nach <B>` · `taste` · `tippe` · `menu` · `drop` · `bild` · `warte` ·
`namen`. Send raw hook commands via `> …` (help: `> hilfe`).

**Important:** JUCE allows only one instance. No Aizana instance may be running
before a test run, or launch merely forwards to it and the hook silently fails
to start. The script aborts in that case.

**Elements are found by NAME**, never by coordinates (component `setTitle` values).
If a name is ambiguous, `find` reports an error and does **not** click
the first match. A silent misclick that reports success is precisely the failure
class this naming system was designed to prevent.

**Painted areas need named components.** What the app only *paints* (drum-editor
pad rows, insert slots) has no name and is therefore inaccessible to both
VoiceOver and test runs. The drum editor uses one Component per row that
**paints nothing and accepts no mouse input** (`setInterceptsMouseClicks(false,false)`).
Clicks, right-clicks and file drops still reach the editor underneath unchanged,
but the row has a name. That name also carries **state** (“Pad Snare” vs. “Pad
Snare (Sample)”); otherwise a purely color-based indicator could only be checked
by pixel comparison.
