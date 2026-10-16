# plugin-abi.md - Phase 3 native plugin ABI decision gate (frozen).
# Module: docs (Markdown).
# Owns: the frozen choice between a C plugin SDK and MCP-as-plugin.
# Depends: docs/roadmap.md Phase 3. Do not reopen without a Phase 4 ADR.

# Plugin ABI decision (frozen)

**Decision:** MCP is the plugin API. antinomia does **not** ship a native
in-process C plugin SDK (no `.so` / `.dll` callbacks, no x64dbg `.dp64`
compatibility layer) in Phase 3 or Phase 4 unless a later ADR explicitly
reopens this gate.

**Date frozen:** Phase 3 Pack E (this document).

## Why MCP-as-plugin

1. **Product bet.** The roadmap differentiates on agentic + headless surfaces
   (TUI + CLI + MCP), not on loading third-party code into the debugger
   process. Process-external helpers already speak the same command table.
2. **Safety.** In-process plugins share the debugger address space with the
   wait-event thread. MCP helpers stay out-of-process; a crashed helper cannot
   take down the session owner.
3. **One surface.** CLI argv, TUI command bar, and MCP `tools/list` already
   share `re_cmd_t`. A second ABI would fork that table and create dual
   maintenance for every new verb.
4. **Slip priority.** Native plugins are explicitly on the "may slip" list;
   MCP side-channel + `state` are identity. Freezing MCP-as-plugin keeps the
   Phase 3 gate honest without pretending a stub SDK is a real ecosystem.

## What "plugin" means under this freeze

| Role | Mechanism |
|---|---|
| Extend automation | MCP client (stdio or `ANTINOMIA_MCP` side-channel attach) |
| Script a session | `script` / command files; on-BP command strings |
| Headless CI | CLI one-shot / script exit JSON (`antinomia/1`) |
| AI helpers | Local deterministic tools (`explain`, `suggest_bp`, `summarize_ex`) on the same table |

External helpers must:

- Speak schema `antinomia/1` JSON (or MCP 2025-06-18 frames wrapping it).
- Never block the wait-event thread; side-channel attach queues through the
  session lock (`re_dbg_lock_submit`).
- Treat privilege / Yama denials as structured `error.code` JSON, not free text
  only.

## Explicitly out of scope (until a new ADR)

- Loading `.so` / `.dll` plugins into `antinomia`
- Exporting a stable C callback ABI (`cbStop`, `cbBreakPoint`, menu hooks)
- Binary compatibility with x64dbg plugin SDKs
- Guaranteeing in-process menu -> TUI action hooks

## Reopen criteria (Phase 4+)

Reopen only with an ADR that names: (1) a concrete first-party need that MCP
cannot meet, (2) a versioned C header under `include/`, (3) a sample out-of-tree
plugin and CI load test on Linux and Windows. Until then, treat this file as
the ABI contract: **MCP is the plugin API.**
