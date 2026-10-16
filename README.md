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
| **Run it** | Opens the empty debugger view |
| **Drop a file on it** | Explorer passes the path; it opens the welcome onto that path |
| **`antinomia shell`** | A prompt, for driving the debug commands one at a time |

Inside the view: <kbd>q</kbd> or <kbd>Esc</kbd> leaves. Phase 1 fills the panes from a
live inferior.

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
default of `1`). See [`docs/yama-ptrace.md`](docs/yama-ptrace.md).

## Commands

Every command also works as a plain command, so nothing is locked inside a session.
Phase 0 stubs return clear not-implemented JSON and still exercise the backend
vtable so both OSes link.

| Command | What it answers |
|---|---|
| `gui [file]` | Full screen empty debugger view, in the terminal |
| `shell` | Line oriented prompt for driving the debug commands |
| `status` | Session status: attached flag, pid, backend stub name |
| `launch <file>` | Launch a target under the debugger (stub) |
| `attach <pid>` | Attach to a running process (stub) |
| `detach` | Detach from the current target (stub) |
| `wait_event` | Wait for the next debug event (stub) |
| `cont` | Continue execution (stub) |
| `step` | Single step (stub) |
| `regs` | Read GPRs + RIP/RSP/RFLAGS |
| `set_regs` | Write GPRs as `name=value` pairs |
| `d [addr]` | Hex dump at address (default RIP) |
| `u [addr]` | Disassemble at address (default RIP) |
| `mem_read` | Read target memory as hex |
| `mem_write` | Write hex bytes; invalidates disasm cache |
| `threads` | List threads (stub) |
| `modules` / `lm` | List loaded modules (maps / load events) |
| `sym` | Resolve `module:symbol` via PE export / ELF dynsym |
| `bt` | Stack walk v0 (frame pointer or RSP dump) |
| `help` | Print usage |
| `version` | Print version and schema |
| `mcp` | Run the MCP server over stdio |

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

For a future live GUI side-channel, set `ANTINOMIA_MCP` to a `tcp:` endpoint or unix
path and use `antinomia mcp --attach` (Phase 3). Phase 0 is stdio only.

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
