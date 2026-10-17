# soak.md - crash-free random step/BP soak on a fixture.
# Module: docs (Markdown).
# Owns: CI-bounded soak policy and full 1h soak instructions.
# Depends: scripts/soak_fuzz.sh; Linux loop_inferior fixture; headless CLI.

# Crash-free soak (random step / BP)

Phase 4 gate: fuzz random step and breakpoint traffic against a fixture without
crashing the debugger. CI cannot burn a wall-clock hour on every PR, so the
harness is **time-bounded** with a documented full-hour mode.

## Harness

`scripts/soak_fuzz.sh` repeatedly:

1. Launches `loop_inferior` (or `--inferior PATH`) under `antinomia headless`
2. Issues a seeded random mix of `bp` / `bc` / `bplist` / `sti` / `sto` /
   `run`+`wait_event` / `regs` / `d` / `pause`
3. Ends each round with `kill`
4. Fails if the debugger process dies on a signal, or if JSON/`headless` output
   is missing / malformed

Environment / flags:

| Knob | Default | Meaning |
|---|---|---|
| `SOAK_SECONDS` / `--seconds N` | `90` | Wall budget (CI) |
| `SOAK_SECONDS=3600` | — | Full gate soak (1 hour) |
| `SOAK_SEED` / `--seed N` | `1` | Deterministic RNG |
| `SOAK_ROUNDS` / `--rounds N` | unlimited | Cap rounds inside the budget |
| `--antinomia PATH` | build tree binary | Debugger under test |
| `--inferior PATH` | `loop_inferior` | Fixture ELF |

## CI-bounded soak (practical)

Linux PR / main CI runs ~90s after ctest:

```console
$ scripts/soak_fuzz.sh \
    --antinomia build/clang-release/bin/antinomia \
    --inferior build/clang-release/bin/loop_inferior \
    --seconds 90 --seed 1
```

Also exposed as CTest `soak_fuzz` (Linux only; skipped on Windows until a PE
inferior harness matches the script symbols).

## Full 1h soak (gate / release)

On a quiet machine (or overnight job), pin the release binary and run one hour:

```console
$ SOAK_SECONDS=3600 SOAK_SEED=42 scripts/soak_fuzz.sh \
    --antinomia ./antinomia \
    --inferior /path/to/loop_inferior
```

Pass criteria:

- Exit code 0
- No debugger crash / hang beyond per-round timeouts
- Log line `soak: ok rounds=… seconds=… seed=…` on stderr

Record the seed, binary SHA256, and fixture path in the release notes when the
full soak is used as the Phase 4 gate evidence.

## Dual-OS note

Live ptrace soak is **Linux-first** (same fixture as Phase 2/3 demos). Windows
CI still runs package + unit smokes; a PE soak can reuse this script once an
exported marker matching `antinomia_touch` is wired into the Windows fixture
path.
