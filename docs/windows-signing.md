# windows-signing.md - Authenticode and SmartScreen release notes.
# Module: docs (Markdown).
# Owns: signed Windows PE checklist, SmartScreen reputation, admin helper split.
# Depends: docs/release-checklist.md, .github/workflows/release.yml.

# Windows signing and SmartScreen

Pack A ships **unsigned** `clang-cl-release` PE builds (static CRT) until org
code-signing secrets are available. This page is the operator checklist for
when signing lands, and the honest first-run story for unsigned releases.

## Current ship path (unsigned)

1. Build with preset `clang-cl-release` (static CRT).
2. Package via `scripts/package_release.sh --os windows ...`.
3. Publish zip + bare `.exe` + `SHA256SUMS` on the GitHub Release.
4. Release notes must say the PE is unsigned and may trip SmartScreen.

### First-run guidance (do not weaken Defender)

```powershell
# After verifying SHA256SUMS:
Unblock-File .\antinomia.exe
Get-FileHash .\antinomia.exe -Algorithm SHA256
```

- Prefer **Unblock-File** + hash verify over turning off SmartScreen/Defender.
- “More info” → “Run anyway” is acceptable for a hash-checked release binary
  from the project’s GitHub Releases page.
- Do **not** document registry hacks or disabling real-time protection.

## When org secrets exist (signed path)

Checklist for maintainers (zolowhos / release owners):

| Step | Action |
|---|---|
| 1 | Store Authenticode cert material in GitHub Actions secrets (never in git) |
| 2 | After `clang-cl-release` link, `signtool sign /fd sha256 /tr <RFC3161> /td sha256` |
| 3 | `signtool verify /pa antinomia.exe` in CI before packaging |
| 4 | Attach the signed PE (and zip contents) to the Release |
| 5 | Keep `SHA256SUMS` for the signed artifacts |

Timestamping (RFC 3161) is required so signatures remain valid after cert
expiry. Dual-sign (SHA-1 + SHA-256) is optional and usually unnecessary for
Win10+ targets.

## SmartScreen reputation

Reputation accrues on the **signed publisher identity** and download volume.
Unsigned or freshly signed binaries can still warn. Mitigations:

- Consistent publisher name across releases.
- Prefer GitHub Releases HTTPS downloads (not random mirrors).
- Document the expected hash in release notes.

## Run-as-admin helper (separated)

Elevating the debugger is **not** the default install path.

- Normal: run `antinomia.exe` as a standard user; attach/launch what the user
  can already debug.
- Admin: a clearly named optional helper or documented elevated shell
  (`Start-Process -Verb RunAs`) used only when SeDebugPrivilege is required.
- Installer scripts (`install.ps1`) must not silently request admin for the
  daily-driver binary.

## Related

- Reproducible packaging: `docs/release-checklist.md`
- Privacy (no telemetry): `docs/privacy.md`
