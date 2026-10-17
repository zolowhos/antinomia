# commands.md - CLI / MCP / TUI command reference for antinomia.
# Module: docs (Markdown).
# Owns: one-table verb list (usage, aliases, MCP flag) matching re_table.c.
# Depends: src/cli/app/re_table.c; schema antinomia/1.

# Command reference

Every debugger verb lives in one table (`src/cli/app/re_table.c`). The same
entry feeds:

| Surface | How |
|---|---|
| argv | `antinomia <cmd> ...` |
| shell / TUI bar | line parser → `re_cmd_find` |
| MCP | `tools/list` / `tools/call` when `mcp_tool` is true |

Stdout is JSON with `"schema":"antinomia/1"` (or `--format text`). Diagnostics go
to stderr via `re_log_*`. `help`, `version`, `gui`, `shell`, and `mcp` are
shell-only (not MCP tools).

Shared flags (most list/memory tools): `--format json|text`, `--limit N`,
`--offset N` / `--cursor C`, `--off N`, `--len N`, `--no-color`.

Address arguments accept hex (`0x...`), decimal, or an
[expression](expression-grammar.md).

## Session control

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `init` | `launch` | `init <file>` | yes |
| `attach` | | `attach <pid>` | yes |
| `detach` | | `detach` | yes |
| `kill` | | `kill` | yes |
| `wait_event` | | `wait_event [timeout_ms]` | yes |
| `run` | `g`, `cont` | `run` | yes |
| `step` | | `step` | yes |
| `sti` | `t` | `sti` | yes |
| `sto` | `p` | `sto` | yes |
| `pause` | | `pause` | yes |
| `status` | | `status` | yes |
| `caps` | | `caps` | yes |

`init` / `launch` start a new inferior. `attach` is subject to
[Yama / SeDebugPrivilege](yama-ptrace.md). `caps` reports backend bits — see
[backend-caps.md](backend-caps.md).

## Breakpoints

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `bp` | | `bp <addr\|expr>` | yes |
| `bc` | | `bc <addr\|expr>` | yes |
| `be` | | `be <addr\|expr>` | yes |
| `bd` | | `bd <addr\|expr>` | yes |
| `bplist` | | `bplist` | yes |
| `bph` | `bphw` | `bph <addr> [size] [x\|w\|rw]` | yes |
| `bphc` | | `bphc <addr>` | yes |
| `bpm` | | `bpm <addr> [len] [w\|rw]` | yes |
| `bpmc` | | `bpmc <addr>` | yes |
| `bpdll` | | `bpdll <name>` | yes |
| `bpdllc` | | `bpdllc <name>` | yes |
| `bpc` | | `bpc <addr> <expr>` | yes |
| `bplog` | | `bplog <addr> <text>` | yes |
| `bplogc` | | `bplogc <addr> <expr>` | yes |
| `bpcmd` | | `bpcmd <addr> <cmd>` | yes |
| `bpcmdc` | | `bpcmdc <addr> <expr>` | yes |
| `bpfr` | | `bpfr <addr> <0\|1>` | yes |

Hardware / memory fidelity limits: [bp-fidelity.md](bp-fidelity.md). Conditions
and log/cmd strings use the [expression grammar](expression-grammar.md).

## Expressions and watches

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `expr` | `?` | `expr <expression>` | yes |
| `var` | | `var <name> [value]` | yes |
| `watch` | `wa` | `watch <expression>` | yes |
| `watchc` | | `watchc <index\|\*>` | yes |
| `watchlist` | | `watchlist` | yes |

## Inspect (regs, mem, disasm, map)

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `regs` | | `regs` | yes |
| `set_regs` | | `set_regs name=val...` | yes |
| `d` | | `d [addr] [--len N]` | yes |
| `u` | | `u [addr] [--limit N]` | yes |
| `graph` | `cfg` | `graph [addr]` | yes |
| `find` | `search` | `find <pattern> [--off A] [--len N]` | yes |
| `mem_read` | | `mem_read <addr> [--len N]` | yes |
| `mem_write` | | `mem_write <addr> <hex>` | yes |
| `mem.page` | `mem_page` | `mem.page [addr] [--limit N] [--offset N]` | yes |
| `maps` | `mmap` | `maps [follow <index\|addr>]` | yes |
| `bt` | | `bt` | yes |

## Modules, symbols, annotations

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `modules` | `lm` | `modules` | yes |
| `sym` | | `sym <module:symbol>` | yes |
| `src` | | `src [addr]` | yes |
| `bm` | | `bm <rva> [name]` | yes |
| `bmlist` | | `bmlist` | yes |
| `patches` | | `patches [<rva> <size> <orig> <patch>]` | yes |
| `funcs` | | `funcs [<start> <end> <name>]` | yes |
| `dbsave` | | `dbsave` | yes |
| `a` | `asm` | `a <addr> <asm...>` | yes |
| `patchlist` | | `patchlist` | yes |
| `patchwrite` | `writeback` | `patchwrite [out]` | yes |

## Threads and OS views

| Command | Aliases | Usage | MCP | Notes |
|---|---|---|---|---|
| `threads` | | `threads` | yes | |
| `thread` | `switch` | `thread <tid>` | yes | |
| `suspend` | | `suspend [tid]` | yes | |
| `resume` | | `resume [tid]` | yes | |
| `seh` | | `seh` | yes | Windows only |
| `handles` | | `handles` | yes | Windows only |
| `signals` | `sigdisp` | `signals` | yes | Linux only |

Wrong-OS tools return `RE_E_UNSUPPORTED` with `"implemented":false`.

## Exceptions / signals policy

| Command | Usage | MCP |
|---|---|---|
| `exbp` | `exbp <code> [--handle]` | yes |
| `exignore` | `exignore <lo> [hi]` | yes |
| `expass` | `expass <lo> [hi]` | yes |
| `exclear` | `exclear <lo> [hi]` | yes |
| `exlist` | `exlist` | yes |

Codes are Win exception codes or Linux signal numbers depending on backend.

## Trace and replay

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `trace` | `tr` | `trace [start\|stop\|clear\|step\|save\|load\|query]` | yes |
| `tracestart` | | `tracestart` | yes |
| `tracestop` | | `tracestop` | yes |
| `traceclear` | | `traceclear` | yes |
| `tracestep` | | `tracestep [N]` | yes |
| `tracesave` | | `tracesave <file>` | yes |
| `traceload` | | `traceload <file>` | yes |
| `trace.query` | | `trace.query [--limit N] [--offset N\|--cursor C]` | yes |
| `traces` | | `traces [clear]` | yes |
| `replay.open` | | `replay.open <file>` | yes |
| `replay.close` | | `replay.close` | yes |
| `replay.query` | | `replay.query [--limit N] [--offset N\|--cursor C]` | yes |
| `replay.frame` | | `replay.frame <index>` | yes |

## Agent / script / AI helpers

| Command | Aliases | Usage | MCP |
|---|---|---|---|
| `state` | | `state [--limit N]` | yes |
| `triage-live` | `triage_live` | `triage-live [--limit N]` | yes |
| `script` | `source` | `script <file>` | yes |
| `headless` | | `headless <file>` | yes |
| `note` | | `note <text>` | yes |
| `notes` | | `notes` | yes |
| `log` | | `log [text]` | yes |
| `explain` | `ai.explain` | `explain [addr]` | yes |
| `suggest_bp` | `ai.suggest_bp` | `suggest_bp [--limit N]` | yes |
| `summarize_ex` | `ai.ex`, `exsum` | `summarize_ex [code]` | yes |

ida-mcp shims (`open_database`, `get_bytes`, `list_funcs`, stubs for
`decompile` / `get_xrefs_to` / `execute_query`): [ida-mcp-shims.md](ida-mcp-shims.md).

## Shell-only

| Command | Usage |
|---|---|
| `gui` | `gui [file]` — full-screen TUI |
| `shell` | interactive prompt |
| `help` | usage |
| `version` | name, version, schema |
| `mcp` | MCP stdio / `--attach` / `--side-listen` |

## Related

- [x64dbg-migration.md](x64dbg-migration.md) — verb map for x64dbg users
- [expression-grammar.md](expression-grammar.md)
- [backend-caps.md](backend-caps.md)
- [theme-keys.md](theme-keys.md) — TUI keys (not command-table verbs)
