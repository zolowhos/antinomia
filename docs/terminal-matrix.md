# terminal-matrix.md - ANSI / terminal compatibility matrix and CI smoke.
# Module: docs (Markdown).
# Owns: supported terminals, NO_COLOR policy, CI ANSI smoke pointers.
# Depends: src/utils/tui/re_tui.c, tests/test_tui.c, scripts/ansi_matrix_smoke.sh.

# Terminal matrix (ANSI)

antinomia’s TUI emits SGR via a single style table in `re_tui` (never ad-hoc
escapes in feature code). Colour is gated by `re_tui_want_color()` which
honours `NO_COLOR`.

## Supported environments

| Host | Notes |
|---|---|
| **Windows Terminal** | Preferred on Windows; UTF-8 + VT sequences |
| **conhost** | Legacy console; enable VT where available; UTF-8 via `SetConsoleOutputCP(65001)` |
| **xterm** / **xterm-256color** | Linux default; CI smoke `TERM` |
| **tmux** | Nested TERM; SGR still emitted when colour is on |

## Policy

| Env | Effect |
|---|---|
| `NO_COLOR` set (any value) | No SGR; plain text |
| non-TTY stdout | No colour framing assumptions for pipes |
| MCP stdio mode | Stream mode avoids TTY colour on the MCP channel |

## CI smoke (feasible subset)

Full interactive matrix (WT / conhost / real tmux attach) is manual. CI runs:

1. **Unit:** `ctest` target `ansi` — style table emits `\x1b[` … `\x1b[0m` with
   colour on; none when colour off.
2. **Script:** `scripts/ansi_matrix_smoke.sh` under `TERM=xterm-256color` and
   `TERM=dumb` (+ `NO_COLOR=1`) on Linux PR CI after ctest.
3. **Windows:** unit `ansi` covers the same SGR bytes; host console attach is
   not automated on `windows-2022`.

```console
$ TERM=xterm-256color scripts/ansi_matrix_smoke.sh \
    build/clang-release/bin/antinomia build/clang-release/bin/re_test_ansi
$ NO_COLOR=1 TERM=dumb scripts/ansi_matrix_smoke.sh build/clang-release/bin/antinomia
```

Manual soak: Windows Terminal + conhost on a clean VM; Linux xterm and
`tmux new -s anti` with `antinomia gui` / `privacy`.
