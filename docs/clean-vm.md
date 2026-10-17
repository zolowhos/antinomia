# clean-vm.md - Phase 1 workflow from a release binary on a clean machine.
# Module: docs (Markdown).
# Owns: third-party / clean-VM checklist; CI-automatable subset.
# Depends: scripts/package_release.sh, scripts/phase1_release_smoke.sh, Release assets.

# Clean-VM / release-binary Phase 1 checklist

Gate: a third-party machine completes the Phase 1 debug loop using **only** the
GitHub Release binary (no full source build required for the smoke itself). A
fixture inferior may come from the release archive extras, a separate download,
or a tiny locally compiled sleeper.

## Manual checklist (clean VM)

- [ ] Fresh OS user / VM with no prior antinomia install
- [ ] Download from the tagged Release:
  - Linux: `antinomia-<tag>-linux-x86_64.tar.gz` (+ `SHA256SUMS`)
  - Windows: `antinomia-<tag>-windows-x86_64.zip` (+ `SHA256SUMS`)
- [ ] Verify checksums (`sha256sum -c SHA256SUMS --ignore-missing`)
- [ ] Extract archive; run `./antinomia version` / `antinomia.exe version`
- [ ] Optional install: `./install.sh` or `.\install.ps1`
- [ ] Obtain a tiny inferior (ELF/PE) with a named export, **or** use
      `loop_inferior` / `breakpoint_me` from a source checkout / CI artifact
- [ ] Phase 1 loop (CLI or `headless`):
  1. `init <inferior>`
  2. `wait_event` until stopped
  3. `bp <module:symbol>` (or address)
  4. `run` + `wait_event`
  5. `regs` and `d --len 64` (or `d <hexaddr>`)
  6. `sti` (and optionally `sto`)
  7. `detach` or `kill`
  Keep steps in one `shell` / `headless` session (argv one-shots do not share state).
- [ ] Confirm stdout JSON carries `"schema":"antinomia/1"` and no debugger crash
- [ ] Windows: note SmartScreen on unsigned first run
      ([windows-signing.md](windows-signing.md)); prefer `Unblock-File` after hash check
- [ ] Linux attach to a foreign PID: read [yama-ptrace.md](yama-ptrace.md)
      (launch/init works with common `ptrace_scope=1`)

## What CI automates

| Step | Where |
|---|---|
| Build + ctest | `.github/workflows/pr.yml` / `release.yml` |
| Package archive + symbols stub | `scripts/package_release.sh` in PR + Release |
| Run Phase 1 loop from **packaged** Linux binary + in-tree fixture | `scripts/phase1_release_smoke.sh` (PR Linux job) |
| CI-bounded soak on fixture | `scripts/soak_fuzz.sh` / CTest `soak_fuzz` |

CI is **not** a third-party clean VM. Treat the package + Phase 1 smoke as the
automatable subset; keep the manual checklist for release sign-off.

## One-liner (Linux package + fixture)

After a local `clang-release` build:

```console
$ scripts/package_release.sh --os linux --version v0.1.0 \
    --bin build/clang-release/bin/antinomia --out dist
$ scripts/phase1_release_smoke.sh \
    --archive dist/antinomia-v0.1.0-linux-x86_64.tar.gz \
    --inferior build/clang-release/bin/loop_inferior
```

## Tag / Release cut (maintainers)

1. Land gate PR on `main` (dual-OS CI green).
2. Annotated tag as **zolowhos**:
   `git tag -a v0.1.0 <sha> -m "antinomia v0.1.0"` && `git push origin v0.1.0`
3. `.github/workflows/release.yml` builds both OSes and attaches artifacts.
4. Confirm Release URL + both primary archives; paste checksums into notes.
5. Optional: run full 1h soak (`docs/soak.md`) and tick this checklist on a VM.
