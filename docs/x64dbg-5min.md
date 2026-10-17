# x64dbg-5min.md - five-minute tutorial for x64dbg users.
# Module: docs (Markdown).
# Owns: quick path from release binary to BP/step/dump for x64dbg migrants.
# Depends: docs/x64dbg-migration.md, docs/commands.md, docs/theme-keys.md.

# Five minutes: x64dbg user on antinomia

Goal: open a target, hit a breakpoint, step, dump memory — same muscle memory as
x64dbg, different surface (TUI / CLI / MCP). Full alias table:
[x64dbg-migration.md](x64dbg-migration.md).

## 0. Get a binary (30s)

From a [GitHub Release](https://github.com/zolowhos/antinomia/releases), grab the
archive for your OS:

```console
# Linux
tar -xzf antinomia-v*-linux-x86_64.tar.gz
cd antinomia-v*-linux-x86_64
./antinomia version

# Windows (PowerShell)
Expand-Archive antinomia-v*-windows-x86_64.zip
cd antinomia-v*-windows-x86_64
.\antinomia.exe version
```

Expect JSON with `"schema":"antinomia/1"`. Optional: `./install.sh` or
`.\install.ps1`. Checklist for a clean VM: [clean-vm.md](clean-vm.md).

## 1. Open the CPU view (45s)

```console
$ ./antinomia                  # welcome / empty session
$ ./antinomia gui /path/to/exe # or drop a file on the binary
```

Keys (x64dbg-ish defaults that do not fight the TUI):

| Key | Meaning | x64dbg habit |
|---|---|---|
| F9 / F5 | run | F9 |
| F7 | step into | F7 / `sti` |
| F8 | step over | F8 / `sto` |
| F2 | toggle BP at RIP | F2 |
| F12 | pause | pause |
| Tab | cycle panes | — |
| q / Esc | leave | — |

Themes/keys: [theme-keys.md](theme-keys.md).

## 2. Same loop from the CLI (2 min)

Use a tiny fixture or any local ELF/PE. From a build tree the Linux sleeper is
`build/clang-release/bin/loop_inferior`.

```console
$ ./antinomia shell
antinomia> init /path/to/loop_inferior
antinomia> wait_event 5000
antinomia> bp loop_inferior:antinomia_touch
antinomia> run
antinomia> wait_event 5000
antinomia> regs
antinomia> d --len 64
antinomia> sti
antinomia> sto
antinomia> detach
```

Prefer a single `shell` / `headless` session (each argv invocation is its own
process and does not share the debug session).

```console
$ ./antinomia headless my_session.re
```

Or a script file via `headless` (JSON `exit` on stdout):

```console
$ ./antinomia headless my_session.re
```

## 3. Agent / MCP path (1 min)

```console
$ ./antinomia mcp                 # stdio MCP; tools match the CLI table
$ ./antinomia mcp --attach        # attach to a running TUI/session side channel
```

Same verbs as tools: `init`, `bp`, `run`, `sti`, `regs`, `detach`, …

## Cheat sheet

| You want… | Type |
|---|---|
| Run | `run` / `g` / F9 |
| Step into / over | `sti`/`t` / `sto`/`p` |
| Breakpoint | `bp <expr>` / F2 |
| Registers / dump | `regs` / `d <addr>` |
| Modules / symbols | `lm` / `sym module:name` |
| Leave target alive | `detach` |
| Stop everything | `kill` |

Linux attach FAQ (Yama): [yama-ptrace.md](yama-ptrace.md). Full command list:
[commands.md](commands.md).
