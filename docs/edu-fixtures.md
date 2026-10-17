# edu-fixtures.md - educational MIT dummy inferiors for debugger E2E demos.
# Module: docs (Markdown).
# Owns: inventory, smoke commands, and Windows live-debug gap notes.
# Depends: tests/fixtures/edu_*.c, scripts/edu_fixture_smoke.sh, ctest.

# Educational debug fixtures

Small **MIT-licensed**, clearly labeled dummy programs under `tests/fixtures/`
for teaching and exercising antinomia end-to-end. They are **not malware**: no
network, no persistence tricks, no packing. Safe to run under a debugger on a
dev machine or in CI.

## Inventory

| Binary | Pedagogy | Dual-OS build | Notes |
|---|---|---|---|
| `breakpoint_me` | software BP target + attach sleep loop | yes | Phase 1 fixture |
| `loop` | short bounded loop / step smoke | yes | Phase 1 fixture |
| `loop_inferior` | long-lived Linux integration inferior | Linux only | live `re_test_debug*` |
| `dwarf_src` | DWARF line tables (`-g`) | Linux only | not a live launch target |
| `edu_hello_spin` | hello then spin on `edu_marker` | yes | launch / BP / step |
| `edu_crash` | intentional null deref after `edu_before_crash` | yes | exception / fault demos |
| `edu_threads` | 4-thread lite BP storm on `edu_worker_hit` | yes | multi-thread BP hits |

Marker / hit functions use `noinline` (+ `used` on GCC/Clang) so **clang-release
LTO** cannot inline them away; otherwise a software BP on the exported symbol
never fires.

## Smoke

From a `clang-release` tree:

```console
cmake --preset clang-release
cmake --build --preset clang-release --parallel
ctest --preset clang-release -R 'fixture_edu_|edu_fixture_smoke' --output-on-failure
# or:
scripts/edu_fixture_smoke.sh
```

`edu_fixture_smoke` (Linux) runs headless sessions:
`init` -> `wait_event` -> `bp` -> `run` -> `regs` / `d` / `sti` / `status` ->
`detach` or `kill` against the educational binaries (plus `breakpoint_me`).

## Windows

Fixtures **build** on Windows (`clang-cl-release`) and have bounded CTest
existence smokes (`fixture_edu_*`). Live launch->BP->step against PE inferiors
still follows the Windows backend gap documented in `docs/yama-ptrace.md`
(`DISABLED_debug_launch_bp_step_detach`). Prefer Linux CI for the live edu
smoke until that lands.
