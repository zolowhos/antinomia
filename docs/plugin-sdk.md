# plugin-sdk.md - MCP-as-plugin SDK guide and example.
# Module: docs (Markdown).
# Owns: how to extend antinomia without an in-process C SDK.
# Depends: docs/plugin-abi.md (frozen decision), docs/examples/mcp_plugin.py.

# Plugin SDK (MCP-as-plugin)

Phase 3 froze the plugin ABI as **MCP-as-plugin** (`docs/plugin-abi.md`).
This document is the SDK surface: how to write an external helper that
behaves like a plugin without loading `.so` / `.dll` code into antinomia.

## Surfaces

| Surface | How |
|---|---|
| Stdio MCP | `antinomia mcp` — JSON-RPC frames on stdin/stdout |
| Side-channel | TUI / `mcp --side-listen` + `antinomia mcp --attach` |
| Scripts | `script` / `headless` command files (schema `antinomia/1`) |
| Argv CLI | same `re_cmd_t` names as MCP `tools/list` |

There is **no** C callback header, menu hook, or x64dbg `.dp64` loader.

## Minimal plugin loop

1. Spawn or attach: `antinomia mcp` (or `--attach` to a live session).
2. `initialize` → `tools/list` → `tools/call` for debug verbs.
3. Read `antinomia/1` JSON in tool results; honour `error.code`.
4. Never block the session owner’s wait-event thread (side-channel queues).

## Example

`docs/examples/mcp_plugin.py` is a process-external “plugin”:

- Starts `antinomia mcp` as a subprocess (stdio).
- Calls `privacy` (trust confirm) then `status`.
- Prints compact results on stderr; exits nonzero on schema/tool failure.

```console
$ python3 docs/examples/mcp_plugin.py --antinomia build/clang-release/bin/antinomia
```

Adapt the same pattern for agents, CI gates, or IDE bridges. Prefer the
side-channel attach path when a human TUI already owns the inferior.

## AI helpers as plugin building blocks

Local, deterministic tools (no cloud):

- `explain` / `ai.explain`
- `suggest_bp` / `ai.suggest_bp` (honours `--seed` / `--limit`)
- `summarize_ex` / `ai.ex` / `exsum`
- `privacy` / `trust`

## Reopen note

A native in-process SDK requires a new ADR (versioned `include/` header +
dual-OS load test). Until then, treat MCP + this guide as the only plugin
SDK.
