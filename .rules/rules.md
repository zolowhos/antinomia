# rules.md - canonical rulebook for antinomia. Read this before writing code.
# Module: rules (Markdown).
# Owns: every convention, layering rule, and output contract the project obeys.
# Depends: none. Authoritative; scripts/check.py enforces the mechanical subset.

## 0. Project

`antinomia` is an AI driven debugger for reverse engineering applications,
libraries, or whatever you want. It is a lightweight CLI that agents drive
through MCP. One binary, three faces:

    antinomia <command> [args]    human / agent / shell usage
    antinomia                     full screen TUI (when both ends are a TTY)
    antinomia mcp                 MCP server over stdio

Debugger in. Pure static PE triage out (that is antistrefo). See section 8.

The C symbol prefix stays `re_` and the macro prefix `RE_`. That is deliberate and
not a leftover: the prefix is a link-time namespace guard, `re` still reads correctly
as reverse engineering, and it is short enough to type hundreds of times per file.
The product name is antinomia; the C namespace is `re_`. Do not unify them without a
strong reason.

## 1. Locked conventions

| Rule | Value |
|---|---|
| Project name | `antinomia` |
| Executable | `antinomia` / `antinomia.exe` |
| JSON schema id | `antinomia/1` |
| C symbol prefix | `re_` / `RE_` (kept deliberately, see section 0) |
| Language core | C11 (`src/utils`, `src/features`) |
| Language shell | C++17 (`src/cli`, `src/mcp`) |
| License | MIT |
| Source charset | ASCII only - no smart quotes, no em-dash, no arrows in code |
| Line endings | LF, one final newline, no trailing whitespace, no tabs |
| Indent | 4 spaces |
| Braces | K&R (attach) |
| Column limit | 100 |
| File line cap | 500 (hard, enforced) |
| Function body cap | 60 lines / 4 nesting levels (hard, enforced) |
| Naming | `re_` prefix, `snake_case`, `_t` for types |
| MCP side-channel env | `ANTINOMIA_MCP` |
| Log env | `RE_LOG` |

## 2. Layout

    .rules/rules.md      this file - canonical rulebook
    .clang-format        formatting truth
    .clang-tidy          naming advisory
    .editorconfig        editor parity
    .gitignore
    LICENSE              MIT
    CMakeLists.txt       C11/C++17, LTO; Clang/LLVM primary
    CMakePresets.json    clang-release, clang-debug (primary)
    docs/style.md        human-readable rules
    docs/util-catalog.md util lookup table (read before writing new code)
    docs/roadmap.md      Phase 0-4 pointer
    scripts/check.py     the gate
    scripts/format.ps1   format wrapper
    src/utils/<cat>/      shared utilities, grouped: text mem algo sys json tui cfg
    src/features/debug/   session + opaque OS backend vtable
    src/cli/             argv dispatch, TUI (C++17 / C11)
    src/mcp/             MCP stdio server (C++17)
    tests/               CTest targets
    tests/fixtures/      tiny C inferiors (breakpoint_me, loop)

Include path root is `src/`, so utils are reached as `#include "utils/mem/re_arena.h"`
and features as `#include "features/debug/re_dbg.h"`.

Rule: a folder holding more than 15 files is too cluttered and must be divided
into sub folders by category. Keep every folder at 15 or fewer. Enforced by
check.py over src/.

## 3. Banner spec

Every file starts with exactly four comment lines, fixed slot order. C uses `//`,
scripts and text use `#`. `scripts/check.py` validates the shape.

    <c> <basename> - <one-line purpose, lowercase, no trailing period>
    <c> Module: <module> (<language>).
    <c> Owns: <what this file is responsible for>.
    <c> Depends: <deps or none>. <hard constraint or none>.

`#pragma once` follows the banner in every header, exactly once.

## 4. Layering rules

| # | Rule | Type |
|---|---|---|
| 0 | check the util catalog before writing; function caps; forbidden-pattern scan | script + review |
| 1 | 500 lines per file | script |
| 2 | 4-line banner, fixed slots | script |
| 3 | `#pragma once` in every header, exactly once | script |
| 4 | ASCII-only source bytes | script |
| 5 | LF, one final newline, no trailing whitespace, no tabs | script |
| 6 | no `malloc`/`calloc`/`realloc`/`free`/`strdup` outside `src/utils` | script |
| 7 | no `printf`/`puts`/`fprintf(stdout)` outside `re_jw*`/`re_text*` | script |
| 8 | no `strcpy`/`strcat`/`sprintf`/`strtok`; `memcpy`/`memset`/`memcmp` on sized spans only | script |
| 9 | `re_` prefix on project symbols | clang-tidy (advisory) |
| 10 | clang-format clean | CTest |

### Exceptions

| Exception | Reason |
|---|---|
| `CMakePresets.json` carries no banner | CMake validates the preset schema strictly |
| `LICENSE` carries no banner, and may hold trailing spaces | Legal text |
| A `.py` file may put a shebang on line 1 | Shebang must be first |
| `tests/` is exempt from rules 7 and 8 | Tests exist to call libc and print |
| `src/utils/re_regex_priv.h` and `re_util.h` stay out of the aggregator | One is engine-private, one is the aggregator |

## 5. Rule 0 - reuse first

Before writing any function past 60 lines or 4 nesting levels, read
`docs/util-catalog.md` and grep `src/utils/`. Do not reinvent what exists.

`scripts/check.py` verifies every header in `src/utils/` appears in both
`docs/util-catalog.md` and `src/utils/text/re_util.h`.

## 6. Architecture

1. **C11 core, C++17 shell.** `src/utils` and `src/features` are pure C11.
   `src/cli` and `src/mcp` are C++17 and never reinterpret binary data themselves.
2. **Opaque `re_dbg_backend` vtable.** OS APIs (`ptrace`, Windows Debug API) live only
   behind backend .c files. CLI/MCP/TUI call `re_dbg_session_*` only (never raw OS APIs).
3. **Session arena != per-command arena.** `re_dbg_session_t.arena` owns the backend and
   outlives one CLI/MCP call. Command handlers get a short-lived `re_ctx_t` arena for
   JSON and scratch only; resetting it must not free session state. Session fields
   `pid`, `tid`, and `last_event` are the status surface.
4. **One command table drives both the CLI and MCP.** A single table feeds argv and
   `tools/list`, so the two surfaces cannot drift.
5. **No third-party C++ libraries.** The streaming writer in `src/utils/json/re_json.h`
   is the JSON emitter for the whole project.

## 7. Output contract

- stdout carries exactly one JSON object plus `\n` for report commands.
- In `mcp` mode, stdout is only newline-delimited JSON-RPC frames.
- stderr carries diagnostics. `re_log_*` is the only diagnostic writer; honour `RE_LOG`.
- Errors in JSON mode: `{"schema":"antinomia/1","error":{...}}` on stderr, nonzero exit.
- Every response carries `"schema":"antinomia/1"`.
- Collections carry `count` and `truncated` and accept `--offset`/`--limit`, default 200.

| Code | Meaning |
|---|---|
| 0 | ok |
| 1 | internal error |
| 2 | usage error |
| 3 | not a recognized binary |
| 4 | malformed binary / range |
| 5 | unsupported (including Phase 0 stubs) |
| 6 | I/O error or out of memory |

## 8. Feature scope

| Capability | Verdict | Where |
|---|---|---|
| Opaque debug backend vtable | **Build** | feature/debug |
| Launch(init) / attach / detach / kill / wait / run(g) / step | Build (live Phase 1+) | feature/debug |
| Regs / mem R/W / threads / modules | Build (live Phase 1+) | feature/debug |
| Custom terminal TUI (CPU-like panes) | **Build** | cli/screen, utils/tui |
| MCP stdio + `ANTINOMIA_MCP` side-channel | Build (attach Phase 3) | mcp |
| Pure static PE triage / FLIRT / capa / analyze-all | **Skip** | antistrefo |
| Qt GUI | **Skip** | custom TUI only |
| Kernel debugging / macOS | Skip | post Phase 4 |

**Debugger in.** Do not reintroduce Skip Debugger. Session state may persist; that is
required for a debugger and inverts antistrefo's "no database" rule for debug labels
and breakpoints (Phase 1+).

## 9. Build and CI

Primary toolchain is Clang/LLVM. Presets: `clang-release`, `clang-debug`,
`clang-cl-release`, `clang-cl-debug`, `gcc-debug`. PR CI builds Linux clang-release
and Windows clang-cl-release with ctest. Tagged releases publish
`antinomia-<tag>-linux-x86_64` and `antinomia-<tag>-windows-x86_64.exe`.
