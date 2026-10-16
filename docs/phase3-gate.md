# phase3-gate.md - Phase 3 agent / side-attach acceptance gate.
# Module: docs (Markdown).
# Owns: CI vs interactive coverage for the Phase 3 MCP/TUI gate.
# Depends: scripts/phase3_gate_demo.sh, tests/phase3_gate_*.py, ctest phase3_gate_*.

# Phase 3 gate

## Goal

Cursor/agent can complete a live debug session over MCP without a human TUI, and
a human TUI can share the same session with `mcp --attach`.

## Checklist

| Path | What | CI | Interactive |
|---|---|---|---|
| Agent (stdio) | `antinomia mcp` → init fixture → BP → run → state → sti loop → detach | Linux: `phase3_gate_mcp` | `scripts/phase3_gate_demo.sh mcp` |
| Parallel session | Session owner + `mcp --attach` mutates same session | Linux: `phase3_gate_side` (`mcp --side-listen` stand-in) | TUI: `scripts/phase3_gate_demo.sh tui` |
| Windows live inferior | Same sequences on PE fixture | Not in matrix yet (ELF fixture + POSIX fifo harness) | Manual when Win backend + PE fixture wired |

## Agent path (CI)

`tests/phase3_gate_mcp.py` feeds JSON-RPC `tools/call` frames to `antinomia mcp`
stdio. No TUI. Sequence matches the board gate and Pack C timing notes.

## Parallel path (CI vs demo)

- **CI:** `mcp --side-listen` owns the process-wide session (same `re_dbg_lock`
  owner/drain model as the TUI); `mcp --attach` drives init/bp/state/sti/detach.
- **Interactive:** `antinomia gui` prints `mcp <endpoint>` in the log pane; agent
  attaches with `ANTINOMIA_MCP` or `mcp --attach <endpoint>`. Needs a TTY — not
  asserted in headless CI beyond the side-listen smoke.

## Dual-OS notes

Linux GitHub runners run both gate tests. Windows CI keeps MCP tools/list +
tools/call smokes; live ptrace/Debug-API gate demos stay Linux until a PE
inferior harness lands (same split as `phase3_state_demo`).

## Repo entry points

```console
$ scripts/phase3_gate_demo.sh mcp    # stdio agent loop
$ scripts/phase3_gate_demo.sh side   # side-listen + attach
$ scripts/phase3_gate_demo.sh tui    # print interactive TUI steps
$ ctest --preset clang-release -R 'phase3_gate_'
```
