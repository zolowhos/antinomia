<div align="center">
<img src="Antistrefo.png" width="50%">
<img src="Banner.png" width="50%">
</div>

<p align="center">
  <b>An AI Driven Debugger Framework</b><br>
  Light Weight, Blazing Fast & Easy to setup and get started
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-0.1.0-blue" alt="version 0.1.0">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT licensed">
  <img src="https://img.shields.io/badge/language-C11%20%2B%20C%2B%2B17-blue" alt="C11 and C++17">
  <img src="https://img.shields.io/badge/phase-0%20scaffold-lightgrey" alt="phase 0 scaffold">
</p>

---

## What it is

Antinomia is an AI driven debugger for reverse engineering applications, libraries,
or whatever you want. It has three faces and they are the same program:

- **A full screen view** in the terminal, with CPU-like panes (disasm, regs, dump,
  stack). Clickable, and fully usable from the keyboard. Phase 0 shows the empty
  welcome layout; live inferior control arrives in Phase 1.
- **Reports** on stdout, as JSON for a program (schema `antinomia/1`).
- **An MCP server** over stdio, so an assistant can call the same debug commands as
  tools.

Because it is one binary and one shared command table, the answer does not depend on
how you asked. `status` in the shell, `antinomia status`, and the MCP tool named
`status` all read the same session.

Phase 0 ships dual-OS stub backends that link and return clear not-implemented
results. It does not claim finished live debug yet.

## Quick start

```console
$ antinomia                     # opens the view on its welcome screen
$ antinomia status              # session status JSON on stdout
$ antinomia version             # name, version, schema antinomia/1
```

Three ways in, and the first is usually what you want:

| | |
|---|---|
| **Run it** | Opens the CPU view (disasm, regs, dump, stack, breakpoints, log) |
| **Drop a file on it** | Explorer passes the path; it opens the view onto that path |
| **`antinomia shell`** | A prompt, for driving the debug commands one at a time |

Inside the view: <kbd>Tab</kbd> / <kbd>Shift-Tab</kbd> cycles pane focus, click selects a
pane, <kbd>q</kbd> or <kbd>Esc</kbd> leaves. x64dbg-ish defaults that do not fight the TUI:
<kbd>F9</kbd>/<kbd>F5</kbd> run, <kbd>F7</kbd> step into, <kbd>F8</kbd> step over,
<kbd>F2</kbd> toggle BP at RIP, <kbd>F12</kbd> pause. Themes and keybindings load from
`theme.json`/`keys.json` (or `.ini`) via `ANTINOMIA_THEME` / `ANTINOMIA_KEYS`, the cwd,
or `$XDG_CONFIG_HOME/antinomia/` (Windows: `%APPDATA%/antinomia/`). See
[`docs/theme-keys.md`](docs/theme-keys.md) and [`docs/examples/`](docs/examples/).

## Building

Needs CMake 3.25+, Ninja, and **Clang/LLVM** (C11 + C++17). That is the primary
toolchain on Windows and Linux.

```console
$ cmake --preset clang-release         # -O2 + LTO
$ cmake --build --preset clang-release
$ ctest --preset clang-release
```

On Windows with the MSVC SDK, prefer `clang-cl-release` (static CRT). Primary
presets: `clang-release`, `clang-debug`, `clang-asan`, `clang-cl-release`,
`clang-cl-debug`. Second compiler: `gcc-debug`. Legacy only (not the ship path):
`msvc-release`, `msvc-debug`, `msvc-asan`. Capstone/disasm is optional behind
`-DRE_ENABLE_DISASM=ON` for later panes; Phase 0 defaults it off.

Windows builds are covered by CI (`clang-cl-release` on `windows-2022`).

Linux attach needs a workable Yama `ptrace_scope` (launch works on the common
default of `1`). FAQ: [`docs/yama-ptrace.md`](docs/yama-ptrace.md).

## Commands

Every command also works as a plain command, so nothing is locked inside a session.
The same table feeds argv, the TUI bar, and MCP `tools/list`.

**Full reference:** [`docs/commands.md`](docs/commands.md) ·
**Expressions:** [`docs/expression-grammar.md`](docs/expression-grammar.md) ·
**Caps matrix:** [`docs/backend-caps.md`](docs/backend-caps.md) ·
**x64dbg aliases:** [`docs/x64dbg-migration.md`](docs/x64dbg-migration.md)

| Command | What it answers |
|---|---|
| `gui [file]` | Full screen debugger view in the terminal |
| `shell` | Line oriented prompt for driving the debug commands |
| `status` / `caps` | Session status; backend capability bits |
| `init` / `launch <file>` | Launch a target under the debugger |
| `attach <pid>` | Attach to a running process (see Yama FAQ) |
| `detach` / `kill` | Detach or kill the current target |
| `run` / `g` / `cont` | Continue execution |
| `sti` / `t` / `step` | Step into |
| `sto` / `p` | Step over |
| `pause` | Interrupt the target |
| `bp` / `bc` / `bplist` / `bph` / `bpm` | Software / hardware / DR-backed memory BPs |
| `expr` / `?` / `watch` | Evaluate expressions; watches |
| `regs` / `set_regs` | Read / write GPRs + RIP/RSP/RFLAGS |
| `d` / `u` / `maps` / `bt` | Dump, disasm, memory map, call stack |
| `modules` / `lm` / `sym` / `src` | Modules and symbols |
| `script` / `headless` / `state` | Scripts and agent snapshots |
| `triage-live` | Live triage: modules, entry, imports, anti-debug |
| ida-mcp shims | Optional honest aliases — see `docs/ida-mcp-shims.md` |
| `help` / `version` / `mcp` | Usage, version+schema, MCP server |

### Flags

| Flag | Meaning |
|---|---|
| `--format json\|text` | Output shape. JSON is the default |
| `--no-color` | Plain output. The `NO_COLOR` convention is honoured regardless |
| `--limit N` | Cap the rows. Default 200, which is also the agent context guard |
| `--offset N` | Skip the first N rows |
| `--off N`, `--len N` | Memory commands: start address and byte count |

## Output

Two shapes, chosen by a person or by the thing on the other end.

A person gets a framed report when `--format text` is asked (Phase 0 status is JSON
first). A program gets JSON, one object per call, always carrying `schema` and
`tool`:

```json
{"schema":"antinomia/1","tool":"status","attached":false,"pid":0,"backend":"linux-stub",
 "note":"Phase 0: backends link but do not debug yet"}
```

The same shape is what the MCP server speaks, so nothing has to be re-parsed to move
between them.

## As an MCP server

```console
$ antinomia mcp
```

Speaks MCP over stdio, one JSON object per call. Every debugger command marked for
MCP is available as a tool, so an assistant can drive the same stubs the CLI prints.
The full screen view and the shell prompt are not tools: they need the terminal
stdio lives on.

Live TUI side-channel (Phase 3): the GUI hosts a local MCP endpoint (unix socket on
Linux, or `tcp:127.0.0.1:<port>`). Agents attach without stealing the terminal:

```console
$ antinomia gui                    # owns the session; prints mcp <endpoint> in the log
$ export ANTINOMIA_MCP=unix:/tmp/antinomia-mcp-<pid>.sock
$ antinomia mcp --attach           # or: antinomia mcp --attach tcp:127.0.0.1:52665
```

`ANTINOMIA_MCP` may be a `tcp:host:port` or `unix:/path` endpoint. When unset,
`--attach` reads the path file written by the GUI (`/tmp/antinomia-mcp.path`).
Headless listen for CI: `antinomia mcp --side-listen` (endpoint on stderr).
MCP tool calls queue into the session owner thread so they never block `wait_event`.

Phase 3 gate demos (Linux CI + repo script):

```console
$ scripts/phase3_gate_demo.sh mcp    # MCP stdio: init→bp→run→state→sti→detach
$ scripts/phase3_gate_demo.sh side   # side-listen + mcp --attach same session
$ scripts/phase3_gate_demo.sh tui    # interactive TUI attach steps
```

See [docs/phase3-gate.md](docs/phase3-gate.md) for CI vs interactive coverage.

**Plugin ABI (frozen):** MCP is the plugin API - no in-process C SDK. See
`docs/plugin-abi.md` and the SDK guide [`docs/plugin-sdk.md`](docs/plugin-sdk.md)
(example: `docs/examples/mcp_plugin.py`). Local AI helpers on the same command
table: `explain`, `suggest_bp`, `summarize_ex` (aliases `ai.explain` /
`ai.suggest_bp` / `ai.ex`).

**Privacy:** no telemetry. Confirm with `antinomia privacy` (or `trust`);
analysis accepts deterministic `--seed` — see [`docs/privacy.md`](docs/privacy.md).

## Releases

Tagged `v*` releases build dual-OS packages via `.github/workflows/release.yml`
(`clang-release` / `clang-cl-release`). Locally:

```console
$ scripts/package_release.sh --os linux --version v0.1.0 \
    --bin build/clang-release/bin/antinomia --out dist --with-symbols-stub
```

See [docs/release-checklist.md](docs/release-checklist.md) for the reproducible
tag → archive → verify steps (archives, bare binaries, symbols stubs, `SHA256SUMS`).
Changelog: [CHANGELOG.md](CHANGELOG.md). Five-minute x64dbg tutorial:
[docs/x64dbg-5min.md](docs/x64dbg-5min.md). Clean-VM Phase 1 checklist:
[docs/clean-vm.md](docs/clean-vm.md). Soak: [docs/soak.md](docs/soak.md).
Windows Authenticode / SmartScreen notes: [docs/windows-signing.md](docs/windows-signing.md).
Terminal ANSI matrix: [docs/terminal-matrix.md](docs/terminal-matrix.md).

## Development

```console
$ powershell -NoProfile -ExecutionPolicy Bypass -File scripts\format.ps1
$ python scripts\check.py .
$ cmake --build --preset clang-release
$ ctest  --preset clang-release
```

The gate is mechanical and it is not advisory. Rules cover line and function caps,
file banners, ASCII-only source, arena-only allocation, and where output is allowed
to be written. Two of those cost real time the first time and are written down in
`.cursor/rules/`:

- Source is **ASCII only**. Box and bullet glyphs go in as byte escapes.
- The formatter **reflows long banner lines**, which can push the fourth line of a file
  banner off the top and fail the build.

`tests/` is exempt from the output and string-call rules, so tests may use `printf` and
`strcmp`. Everything else is expected to hold.

See [docs/roadmap.md](docs/roadmap.md) for the Phase 0-4 spine.

## Licence

MIT. See [LICENSE](LICENSE).
