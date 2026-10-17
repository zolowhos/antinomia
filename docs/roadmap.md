# roadmap.md - short Phase 0-4 product spine for antinomia.
# Module: docs (Markdown).
# Owns: the in-repo roadmap pointer; full detail lives in Project Context.
# Depends: none. Full roadmap: docs/x64dbg-challenger-roadmap.md in Project store.

# Roadmap

antinomia is an AI-driven debugger for reverse engineering: **TUI + CLI + MCP**, dual Linux/Windows, with an opaque OS debug backend (`ptrace` / Windows Debug API) behind a C11 session core.

The full product roadmap (competitive bar, Linux parity gaps, per-phase checklists, and acceptance gates) lives in **Project Context**:

`docs/x64dbg-challenger-roadmap.md`

This file is the short in-repo pointer. Phases below are the product spine.

## Phases

| Phase | Name | One-line gate |
|---|---|---|
| **0** | Scaffold / governance / stub | Dual-OS CI green; empty TUI + MCP hello; **no live inferior** |
| **1** | MVP debugger | Launch → software BP → step → dump on PE **and** ELF fixtures |
| **2** | x64dbg-competitive core | Conditional BP + HW watch + symbols + map/threads/trace v1 |
| **3** | Differentiation (AI / MCP) | Agent completes a debug session via MCP; TUI side-channel attach works ([phase3-gate.md](phase3-gate.md)) |
| **4** | Polish / packaging | Tagged dual-OS release usable on clean VMs |

### Phase 0 (current code goal)

CMake presets, rules gate, utils + TUI + CLI + MCP shell, stub `an_debug_backend_t`, dual-OS CI, schema `antinomia/1`. Commands: `version`, `help`, empty `gui`, `mcp` tools list.

### Phase 1

Launch/attach, software breakpoints, step in/over, regs, memory R/W, modules + export/dynsym symbols, persistence v0, CPU-like TUI panes, fixture inferior + headless integration tests on Linux and Windows.

### Phase 2

Hardware/memory/module-load BPs (`bph`/`bpm`/`bpdll`, `caps.hw_bp`/`caps.mem_bp`; see
`docs/bp-fidelity.md`), expression engine, conditional BP, PDB (dbghelp) / DWARF
line tables behind `re_sym_provider_t`, richer user DB (bookmarks/patches/funcs),
map/threads/graph/trace panes, PE/ELF patch write-back.

### Phase 3

Every debug verb as MCP tool; side-channel attach; bounded `state` snapshots
(regs + disasm window + open BPs + last N log lines); `trace.query` / `mem.page`
with hard limits and `next_cursor`; `triage-live`; honest ida-mcp shims
(`docs/ida-mcp-shims.md`); `headless` script mode with JSON `exit`; native
plugin ABI **frozen** as MCP-as-plugin (see `docs/plugin-abi.md`); local AI
helpers `explain` / `suggest_bp` / `summarize_ex`.

### Phase 4

Installable archives (`scripts/package_release.sh`, tagged Release workflow), optional
symbols package stubs, reproducible checklist in `docs/release-checklist.md`,
themes/keybindings, performance and soak hardening, docs (command ref, expression
grammar, Yama FAQ in `docs/yama-ptrace.md`), x64dbg user migration aliases.
Trust: telemetry-free `privacy` tool + `--seed` (`docs/privacy.md`), MCP plugin
SDK example (`docs/plugin-sdk.md`), Windows signing notes
(`docs/windows-signing.md`), terminal ANSI matrix (`docs/terminal-matrix.md`).

## Architecture invariants

- Opaque backend vtable — never leak OS APIs into CLI/MCP/TUI.
- One command table feeds argv, MCP `tools/list`, and the TUI command bar.
- Session arena ≠ per-command arena.
- Linux ELF is first-class (not a Windows afterthought).

## Non-goals (until post–Phase 4)

Kernel debugging, macOS, Qt GUI clone, full x64dbg plugin binary compatibility, becoming a static analyzer (that is antistrefo).

## Identity vs slip

**Never slip:** backend vtable, launch/attach, software BP, regs, mem R/W, disasm pane, CLI=MCP command table.

**May slip:** graph, SEH/handles, IAT repair, Yara, decompiler, full DWARF types, native plugins.

**Differentiate early:** MCP side-channel + `state` snapshot (can overlap late Phase 2).
