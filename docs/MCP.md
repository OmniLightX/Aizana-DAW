# Connecting Claude or Codex to Aizana (MCP)

Aizana ships an **MCP (Model Context Protocol) server** — `aizana-mcp` — that lets an
AI assistant (Claude Code, Claude Desktop, OpenAI Codex, or any MCP client) author
brand-new audio plugins for Aizana from a free-text description.

This is the **opt-in** AI path. Aizana's built-in local composer still works with no AI
and no network. The MCP server only adds the ability for an LLM to write arbitrary Faust
DSP, which Aizana compiles into a real CLAP plugin and drops into your plugin library.

```
You (in Claude/Codex): "make me a gritty lo-fi telephone vocal effect"
   → LLM calls faust_reference, writes Faust DSP, calls compile_plugin
   → aizana-mcp compiles it → ~/Library/.../Aizana/Generated Plugins/<name>.clap
   → running Aizana picks it up automatically → insert it from the plugin browser
```

The client can generate plugins and invoke the live-mixing tools listed below.
Live changes use Aizana’s command bridge; audio processing remains local.

---

## Two ways to use it

**A) In-app AI Studio (UI label: KI-Studio; recommended, built in).** Aizana ships a chat panel — open it via
**Song → „KI-Studio…"**. It spawns *your* installed `claude` (or `codex`) CLI as the agent
(login-shell wrapped, so it inherits your PATH and login — no API key) with the bundled
`aizana-mcp` registered automatically. Describe a plugin in the chat → it's built and shows
up live in the **"Aizana"** plugin group. Requirements: `brew install faust` and an
installed, logged-in `claude`/`codex` CLI. Nothing else to configure.


**B) External Claude Code / Codex (your own terminal).** Register `aizana-mcp` with your CLI
yourself and generate plugins from any project — see the setup below.

---

## 1. Prerequisites

- **Faust compiler** on your machine: `brew install faust`
- The **`aizana-mcp`** binary, built together with the app:
  ```
  cmake --build build --target aizana-mcp
  ```
  Result: `build/aizana-mcp_artefacts/Release/aizana-mcp`
  (a `GeneratorToolchain/` folder with the CLAP headers is placed next to it).

> The server finds its toolchain in this order: `AIZANA_TOOLCHAIN_DIR` env →
> the sibling `GeneratorToolchain/` folder → an installed `/Applications/Aizana.app`.
> If you move the binary, keep `GeneratorToolchain/` beside it or set the env var.

Use the **absolute path** to the binary in the steps below. For example:
```
/path/to/Aizana/build/aizana-mcp_artefacts/Release/aizana-mcp
```

---

## 2. Claude Code (CLI)

```bash
claude mcp add aizana -- "/ABS/PATH/TO/aizana-mcp"
```

If the binary was relocated away from its `GeneratorToolchain/` folder, pass the toolchain
path explicitly:

```bash
claude mcp add aizana \
  --env AIZANA_TOOLCHAIN_DIR="/ABS/PATH/TO/GeneratorToolchain" \
  -- "/ABS/PATH/TO/aizana-mcp"
```

Verify: `claude mcp list` (should show `aizana`), then in a session ask Claude to
"list the Aizana plugin tools" or just describe a plugin.

## 3. Claude Desktop

Add to `~/Library/Application Support/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "aizana": {
      "command": "/ABS/PATH/TO/aizana-mcp"
    }
  }
}
```

Restart Claude Desktop.

## 4. OpenAI Codex (CLI)

Add to `~/.codex/config.toml`:

```toml
[mcp_servers.aizana]
command = "/ABS/PATH/TO/aizana-mcp"
args = []
# env = { AIZANA_TOOLCHAIN_DIR = "/ABS/PATH/TO/GeneratorToolchain" }
```

---

## 5. Tools the server exposes

**Plugin authoring (write `.clap` files to your library):**

| Tool | What it does |
|------|--------------|
| `faust_reference` | Returns the Faust idioms Aizana supports. The LLM should call this first so the DSP it writes compiles. |
| `compose_plugin`  | `{ prompt }` → a local baseline Faust program for that prompt (a starting point to refine). |
| `compile_plugin`  | `{ name, faust_dsp }` → compiles the DSP into a real `.clap` in your library; returns the path or the compiler error log. |
| `list_plugins`    | Lists the plugins already generated into your library. |

The generated `.clap` files live in
`~/Library/Aizana/Generated Plugins/`
(grouped under the **"Aizana"** manufacturer in the plugin browser). A running Aizana
detects new files within a few seconds; otherwise they appear on next launch.

**Live mixing (act on the session open in Aizana RIGHT NOW):**

| Tool | What it does |
|------|--------------|
| `list_tracks`       | Lists the audio tracks in the open session with each track's volume (dB) and pan. Call this first so you use exact names. |
| `set_track_volume`  | `{ track, db }` → set a track's volume in dB (clamped to [-60, +12]). |
| `set_track_pan`     | `{ track, pan }` → set a track's pan (-1 = L, 0 = C, +1 = R). |
| `mute_track`        | `{ track, muted }` → mute / un-mute a track. |
| `solo_track`        | `{ track, soloed }` → solo / un-solo a track. |
| `set_track_eq`      | `{ track, bands[] }` → add a parametric EQ (one instance per call, ≤8 bands). Each band: `type` (`high_pass`/`low_shelf`/`high_shelf`/`bell`), `freq`, `gain`, optional `q`. |
| `add_compressor`    | `{ track, threshold_db?, ratio?, makeup_db? }` → insert a built-in compressor (sensible defaults). |
| `add_deesser`       | `{ track, freq_hz?, threshold_db?, ratio? }` → insert a built-in de-esser (tames sibilance). |
| `add_reverb`        | `{ track, amount? }` → send the track to a shared "Reverb" FX bus (created once, 100% wet) — the pro send pattern, not a per-track insert. |

Every move applies immediately as **one undo step**, through the same `AICommandBridge`
path as the AI Mix ("KI-Mix") button.

These require **Aizana to be running** (open it and click the **"KI-Studio"** button so the
live bridge is up). The app advertises a loopback port + token via
`~/Library/Aizana/aistudio/live.json`; `aizana-mcp` reads it and
forwards each request to the running session, which applies the move through the same
`AICommandBridge` path as the "KI-Mix" button (one undo step). If Aizana isn't running, the
tool returns a clear message rather than failing silently. Connections are loopback-only
(`127.0.0.1`) and token-guarded.

---

## 6. Quick manual smoke test (no AI client needed)

Pipe JSON-RPC straight into the binary:

```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05"}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
  | "/ABS/PATH/TO/aizana-mcp"
```

You should see an `initialize` result naming the `aizana` server, then a `tools/list`
result listing the available plugin-generation and live-mixing tools.

---

## Security & privacy

- The server runs **locally** and speaks only to the MCP client that launched it (stdio).
- The server can write generated plugin files and change the open session through
  the documented live tools. Check tool permissions in the connected client.
- External AI clients have their own data-handling policies. Prompts and tool
  responses may be processed by their providers.
- Default plugin hosting is in-process. Optional out-of-process hosting uses a
  separate worker; see [the tested behavior and limits](OOP_EDITOR_WINDOWS_2026-10-20.md).
