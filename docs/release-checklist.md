# release-checklist.md - reproducible dual-OS release steps for antinomia.
# Module: docs (Markdown).
# Owns: tag-to-artifact checklist, archive layout, symbols stubs, verify steps.
# Depends: scripts/package_release.sh, .github/workflows/release.yml, CMake presets.

# Reproducible release checklist

Goal: a tagged GitHub Release with versioned **linux-x86_64** and **windows-x86_64**
packages that a clean VM can use for the Phase 1 workflow. Schema stays
`antinomia/1`.

## Inputs (pin these)

| Input | Expected |
|---|---|
| Git ref | Annotated tag `vMAJOR.MINOR.PATCH` on `main` (or rc on a release branch) |
| Commit SHA | Record the tagged SHA in the release notes |
| Toolchain | Clang/LLVM + Ninja; Windows ship path is **clang-cl** + static CRT |
| Presets | Linux: `clang-release`; Windows: `clang-cl-release` |
| Capstone | Default off (`RE_ENABLE_DISASM=OFF`) unless the tag notes otherwise |
| Author | Commits and tags under **zolowhos** only |

## Build (local or CI)

```console
# Linux
cmake --preset clang-release
cmake --build --preset clang-release --parallel
ctest --preset clang-release --output-on-failure
clang --version | tee build/clang-release/COMPILER.txt

# Windows (msvc-dev-cmd x64 shell)
cmake --preset clang-cl-release
cmake --build --preset clang-cl-release --parallel
ctest --preset clang-cl-release --output-on-failure
clang-cl --version > build/clang-cl-release/COMPILER.txt
```

Gates: `python3 scripts/check.py` clean on the tagged tree; dual-OS ctest green.

## Package

`scripts/package_release.sh` stages archives, bare binaries (legacy CI names),
optional symbols **stubs**, and `SHA256SUMS`:

```console
# Linux host
scripts/package_release.sh \
  --os linux --version v0.1.0 \
  --bin build/clang-release/bin/antinomia \
  --out dist --with-symbols-stub

# Windows host (Git Bash) after clang-cl-release
scripts/package_release.sh \
  --os windows --version v0.1.0 \
  --bin build/clang-cl-release/bin/antinomia.exe \
  --out dist --with-symbols-stub
```

### Artifact names

| File | Role |
|---|---|
| `antinomia-<tag>-linux-x86_64.tar.gz` | Installable archive (`antinomia`, `install.sh`, `LICENSE`, `INSTALL.txt`) |
| `antinomia-<tag>-linux-x86_64` | Bare binary (same basename as Phase 0 CI) |
| `antinomia-<tag>-linux-x86_64-symbols.tar.gz` | Optional symbols **stub** (DWARF placeholder) |
| `antinomia-<tag>-windows-x86_64.zip` | Installable archive (`antinomia.exe`, `install.ps1`, …) |
| `antinomia-<tag>-windows-x86_64.exe` | Bare PE |
| `antinomia-<tag>-windows-x86_64-symbols.zip` | Optional symbols **stub** (PDB placeholder) |
| `SHA256SUMS` | Digests for every staged file except itself |

Symbols packages are stubs until CI emits real DWARF/PDB sidecars. The stub
README documents the reserved layout (`debug/`, `pdb/`).

## CI path (preferred)

1. Merge to `main` with dual-OS PR CI green.
2. `git tag -a vX.Y.Z <sha> -m "antinomia vX.Y.Z"` and push the tag (zolowhos).
3. `.github/workflows/release.yml` builds both OSes, runs ctest, packages via
   `scripts/package_release.sh --with-symbols-stub`, and attaches all artifacts
   to the GitHub Release for that tag.
4. `workflow_dispatch` with an existing tag rebuilds and re-attaches assets.

## Verify on a clean machine

Full third-party checklist: **`docs/clean-vm.md`**. Tutorial for x64dbg users:
**`docs/x64dbg-5min.md`**. Changelog: **`CHANGELOG.md`**. Soak: **`docs/soak.md`**.

```console
# Linux
tar -xzf antinomia-vX.Y.Z-linux-x86_64.tar.gz
cd antinomia-vX.Y.Z-linux-x86_64
./antinomia version
./install.sh   # optional; needs ~/.local/bin on PATH
sha256sum -c ../SHA256SUMS --ignore-missing

# Windows (PowerShell)
Expand-Archive antinomia-vX.Y.Z-windows-x86_64.zip
cd antinomia-vX.Y.Z-windows-x86_64
.\antinomia.exe version
.\install.ps1
```

Smoke the Phase 1 loop from the release binary alone: launch fixture → BP →
regs/dump → step → detach. Prefer the headless CLI/MCP path if no TTY.
CI automates the Linux package path via `scripts/phase1_release_smoke.sh`.

## Signing / SmartScreen

Full Authenticode signing needs org secrets (deferred automation). Operator
checklist, SmartScreen first-run notes, and the separated run-as-admin helper
policy live in **`docs/windows-signing.md`**.

Until secrets exist:

- Ship unsigned `clang-cl-release` PE with static CRT.
- Document in the release body that Windows may show SmartScreen on first run.
- Do not recommend disabling Defender; prefer `Unblock-File` after hash check.
- Link `docs/windows-signing.md` from the release notes.

## Tag cut (zolowhos)

```console
git checkout main && git pull
git tag -a v0.1.0 <sha> -m "antinomia v0.1.0"
git push origin v0.1.0
# release.yml attaches linux + windows archives to the GitHub Release
```

If the push tag permission is unavailable from CI bots, cut the annotated tag
from a zolowhos workstation after merging the gate PR. Rebuild assets with
Actions → Release → `workflow_dispatch` and the tag name.

## Release notes template

```text
antinomia <tag> (<full sha>)

Artifacts: linux-x86_64 archive + bare; windows-x86_64 zip + exe;
optional symbols stubs; SHA256SUMS.

Presets: clang-release / clang-cl-release. Capstone default off.
Checklist: docs/release-checklist.md
Changelog: CHANGELOG.md
Tutorial: docs/x64dbg-5min.md
Clean VM: docs/clean-vm.md
Soak: docs/soak.md
```
