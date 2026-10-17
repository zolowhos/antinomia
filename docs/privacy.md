# privacy.md - telemetry-free confirm and deterministic analysis seeds.
# Module: docs (Markdown).
# Owns: privacy contract, --seed policy, machine-readable privacy tool.
# Depends: docs/plugin-sdk.md, src/cli/cmds/ai/re_cmds_ai.c.

# Privacy and deterministic analysis

antinomia does **not** phone home. There is no telemetry, crash uploader,
usage analytics, or cloud AI path in the tree. Local AI helpers
(`explain`, `suggest_bp`, `summarize_ex`) run offline on the same command
table as CLI/MCP.

## Machine confirm

```console
$ antinomia privacy
$ antinomia trust --seed 42
```

JSON (schema `antinomia/1`) always includes:

| Field | Meaning |
|---|---|
| `telemetry` | always `false` |
| `analytics` | always `false` |
| `network_phone_home` | always `false` |
| `cloud_ai` | always `false` |
| `local_analysis_only` | always `true` |
| `deterministic` | analysis helpers are reproducible |
| `seed_set` / `seed` | present when `--seed N` was passed |

MCP tool name: `privacy` (alias `trust` on the argv table).

## `--seed`

Global flag: `--seed N` (decimal or `0x` hex). Honoured by analysis tools:

- Without `--seed`: fixed catalog / decode order (stable across runs).
- With `--seed`: `suggest_bp` Fisher-Yates shuffles resolved sites via
  `re_rng_*` before applying `--limit`, so the same seed + limit yields the
  same subset. `explain` / `summarize_ex` remain content-deterministic and
  echo `seed` / `seed_set` for audit trails.

```console
$ antinomia suggest_bp --seed 7 --limit 3
$ antinomia privacy --seed 7
```

## What is out of scope

- No opt-in telemetry channel.
- No network client in the debugger binary for metrics or model calls.
- External MCP plugins may use the network; that is the plugin process, not
  antinomia. See `docs/plugin-sdk.md`.
