# ida-mcp-shims.md - honest ida-mcp shaped MCP/CLI tools.
# Module: docs (Markdown).
# Owns: real-vs-stub map for optional ida-mcp compatibility shims.
# Depends: command table in src/cli/app/re_table.c; schema antinomia/1.

antinomia optionally advertises a few **ida-mcp-shaped** tool names so agents that
already speak ida-mcp can attach without inventing a second dialect. Shims must
never lie: unsupported IDA features return `implemented: false` and
`RE_E_UNSUPPORTED` rather than fabricated decompiler text or xrefs.

| Tool | Status | Maps to / note |
|---|---|---|
| `open_database` | **real** | `init` / `launch` — starts a **live** debuggee. Not an IDA `.i64`/`.idb`. |
| `get_bytes` | **real** | `mem_read` — process memory bytes as hex. |
| `list_funcs` | **real (limited)** | `funcs` — session function-boundary cache only; not IDA auto-analysis. |
| `decompile` | **stub** | No decompiler. Refuses fake pseudocode; use `u` / `graph`. |
| `get_xrefs_to` | **stub** | No xref index. Refuses fake edges. |
| `execute_query` | **stub** | No IDC/IDAPython. Use named antinomia tools or `script`/`headless`. |

Every response carries `"schema":"antinomia/1"`. Real shims also set
`"shim":"ida-mcp"` and `"maps_to"` so agents can tell compatibility aliases from
native verbs (`triage-live`, `state`, `bp`, …).

Prefer native tools for new agent scripts. Keep ida-mcp names only where they
reduce friction without promising IDA parity.
