# CHANGELOG.md - release notes for tagged antinomia builds.
# Module: docs (Markdown).
# Owns: user-facing changelog per version tag.
# Depends: docs/release-checklist.md; GitHub Release body.

# Changelog

All notable changes for tagged releases are recorded here. Schema remains
`antinomia/1`. Artifact layout: `docs/release-checklist.md`.

## [v0.1.1] - 2026-10-17

Patch release on `main` tip after v0.1.0. Schema stays `antinomia/1`.

### TUI (antistrefo alignment)

- Align CPU TUI chrome with antistrefo menus and buttons (`re_gui_menu`, paint)
- README brand image update

### Educational fixtures

- Add educational debug fixtures and headless E2E smoke (`edu_threads`, related)
- Format-gate fixes for fixture sources

### Artifacts (expected on the GitHub Release)

| File | Role |
|---|---|
| `antinomia-v0.1.1-linux-x86_64.tar.gz` | Linux archive |
| `antinomia-v0.1.1-windows-x86_64.zip` | Windows archive |
| `SHA256SUMS` | Digests |

Bare binaries and symbols stubs may also be attached by the release workflow.

## [v0.1.0] - 2026-10-17

Phase 4 gate release. Base: Wave 2 tip `c6db597` on `main`, plus this gate's
changelog / tutorial / soak / clean-VM docs. Record the final annotated-tag SHA
in the GitHub Release body when cutting `v0.1.0`.

### Packaging

- Dual-OS installable archives via `scripts/package_release.sh`
  (`linux-x86_64` tar.gz + bare; `windows-x86_64` zip + exe)
- Optional symbols **stubs** and merged `SHA256SUMS` from `.github/workflows/release.yml`
- Reproducible checklist: `docs/release-checklist.md`
- Clean-VM / release-binary Phase 1 workflow: `docs/clean-vm.md`

### Debugger / robustness (Phases 1-4 summary)

- Launch/attach, software + HW/memory/DLL BPs, step in/over, regs, dump, maps
- Dual-OS backends behind `re_dbg_backend` (Linux ptrace / Windows Debug API)
- Expression language, conditional BP, modules/symbols, trace v1, scripts/`headless`
- MCP tools + side-channel attach; `state` / `triage-live`; privacy + `--seed`
- Themes/keybindings; perf (disasm cache, pane redraw, event coalesce)
- Robustness: inferior exit races, BP storms, large module lists

### Docs for this gate

- 5-minute x64dbg-user tutorial: `docs/x64dbg-5min.md`
- Soak (CI-bounded + full 1h): `docs/soak.md`, `scripts/soak_fuzz.sh`
- Command ref, expression grammar, caps matrix, Yama FAQ, x64dbg migration table

### Artifacts (expected on the GitHub Release)

| File | Role |
|---|---|
| `antinomia-v0.1.0-linux-x86_64.tar.gz` | Linux archive |
| `antinomia-v0.1.0-windows-x86_64.zip` | Windows archive |
| `SHA256SUMS` | Digests |

Bare binaries and symbols stubs may also be attached by the release workflow.
