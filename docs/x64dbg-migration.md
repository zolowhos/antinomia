# x64dbg-migration.md - x64dbg → antinomia command alias table.
# Module: docs (Markdown).
# Owns: migration map for x64dbg users (verbs, keys, gaps).
# Depends: docs/commands.md, docs/expression-grammar.md, docs/theme-keys.md.

# x64dbg migration alias table

antinomia is **not** a Qt clone of x64dbg. It is a dual-OS debugger (Linux ELF +
Windows PE) with TUI + CLI + MCP on one command table. This page maps familiar
x64dbg verbs to antinomia equivalents so an x64dbg user can be productive in
minutes.

Full command list: [commands.md](commands.md). Keys: [theme-keys.md](theme-keys.md).

## Control / session

| x64dbg (approx.) | antinomia | Notes |
|---|---|---|
| InitDebug / open EXE | `init` / `launch` | Also ida-mcp shim `open_database` |
| AttachDebugger | `attach <pid>` | Linux: [Yama FAQ](yama-ptrace.md) |
| DetachDebugger | `detach` | Leaves process running |
| StopDebug | `kill` | Stops debug and kills target |
| Run / F9 | `run` / `g` / `cont` | TUI: F9 or F5 |
| Pause / F12 | `pause` | |
| StepInto / `sti` / `t` / F7 | `sti` / `t` / `step` | |
| StepOver / `sto` / `p` / F8 | `sto` / `p` | |
| StepOut / `rtr` | — | **Not shipped** yet |
| Wait for event | `wait_event` | Explicit in CLI/MCP; TUI waits internally |

## Breakpoints

| x64dbg | antinomia | Notes |
|---|---|---|
| `bp` / F2 toggle | `bp` / TUI F2 at RIP | Software BP |
| `bc` | `bc` | Clear by address/expr |
| `bd` / `be` | `bd` / `be` | Disable / enable |
| `bplist` | `bplist` | |
| `bph` / hardware | `bph` / `bphw` | 4 DR slots; see [bp-fidelity.md](bp-fidelity.md) |
| `bphc` | `bphc` | |
| Memory BP / page guard | `bpm` | **DR-backed only** (no PAGE_GUARD/`mprotect` yet) |
| DLL load BP | `bpdll` / `bpdllc` | Substring match |
| Conditional BP | `bpc <addr> <expr>` | [expression-grammar.md](expression-grammar.md) |
| Log on hit | `bplog` / `bplogc` | |
| Command on hit | `bpcmd` / `bpcmdc` | |
| Fast resume | `bpfr` | |
| Exception BP | `exbp` / `exignore` / `expass` | Win codes / Linux signos |

## Inspect

| x64dbg | antinomia | Notes |
|---|---|---|
| Dump `d` | `d` | `--len` for size |
| Disasm `u` | `u` | |
| Registers | `regs` / `set_regs` | GPRs + RIP/RSP/RFLAGS |
| Memory map | `maps` / `mmap` | |
| Modules `lm` | `modules` / `lm` | |
| Symbols | `sym <module:symbol>` | Export/dynsym; PDB/DWARF via `src` |
| Call stack | `bt` | |
| Threads | `threads` / `thread` / `switch` | `suspend` / `resume` |
| Graph | `graph` / `cfg` | Function-local CFG |
| Find / search | `find` / `search` | hex / `"str"` / `u32:` patterns |
| Assemble / patch | `a` / `asm`, `patchlist`, `patchwrite` | |

## Expressions

| x64dbg habit | antinomia | Notes |
|---|---|---|
| `? expr` calculator | `expr` / `?` | Same idea |
| Register names | `rax`, `rip`, … | Case-insensitive |
| `[addr]` dereference | `[expr]` / `dword:[expr]` | Width prefixes supported |
| `module.dll:Symbol` | `module:symbol` | |
| User vars | `$name` + `var` | |
| Watches | `watch` / `wa` / `watchlist` | |

Differences: no assignment inside expressions; logical `&&`/`||` evaluate both
sides; no step-out sugar. Details: [expression-grammar.md](expression-grammar.md).

## Scripting / automation (the big shift)

| x64dbg | antinomia |
|---|---|
| Script window / `.txt` cmds | `script` / `source` / `headless` |
| Plugins (`.dp64`) | **MCP-as-plugin** (frozen) — [plugin-abi.md](plugin-abi.md) |
| — | `antinomia mcp` + `tools/call`; TUI `mcp --attach` |
| — | `state`, `triage-live`, `trace.query`, `mem.page` |

There is **no** in-process x64dbg plugin ABI. Automate with MCP or command
files on the shared table.

## OS-specific views

| x64dbg-ish | antinomia | Host |
|---|---|---|
| SEH chain | `seh` | Windows |
| Handles | `handles` | Windows |
| Signals | `signals` / `sigdisp` | Linux |
| Capability probe | `caps` | both — [backend-caps.md](backend-caps.md) |

## Keyboard defaults (TUI)

Built-in x64dbg-ish keys that do not fight the TUI:

| Key | Action |
|---|---|
| F9 / F5 | run |
| F7 | step into |
| F8 | step over |
| F2 | toggle BP at RIP |
| F12 | pause |
| Tab / Shift-Tab | cycle panes |
| Esc / q | leave GUI |

Override via `keys.ini` / `keys.json` — [theme-keys.md](theme-keys.md).

## Mental model checklist

1. One binary, three faces (TUI / CLI JSON / MCP) — same verbs.
2. Linux is first-class; attach may need Yama awareness.
3. Query `caps` before assuming HW/mem/load/exception features.
4. Prefer MCP + scripts over hunting for a plugin SDK.
5. Schema is always `antinomia/1` on stdout JSON.
