#!/usr/bin/env bash
# package_release.sh - stage versioned dual-OS release archives and symbols stubs.
# Module: release (shell).
# Owns: linux-x86_64 / windows-x86_64 archives, optional symbols stubs, SHA256SUMS.
# Depends: built antinomia binary; tar; zip (Windows packages); repo LICENSE.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OS=""
VERSION=""
BIN=""
OUT="${ROOT}/dist"
WITH_SYMBOLS=0

usage() {
  cat >&2 <<'EOF'
Usage: scripts/package_release.sh --os linux|windows --version VER --bin PATH
                                  [--out DIR] [--with-symbols-stub]

Stages versioned release artifacts under --out (default: dist/):

  linux:
    antinomia-VER-linux-x86_64.tar.gz   (binary + LICENSE + install helper)
    antinomia-VER-linux-x86_64          (bare binary, same as CI legacy name)
    antinomia-VER-linux-x86_64-symbols.tar.gz   (optional stub)

  windows:
    antinomia-VER-windows-x86_64.zip
    antinomia-VER-windows-x86_64.exe
    antinomia-VER-windows-x86_64-symbols.zip    (optional stub)

  both:
    SHA256SUMS
EOF
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --os) OS="${2:-}"; shift 2 ;;
    --version) VERSION="${2:-}"; shift 2 ;;
    --bin) BIN="${2:-}"; shift 2 ;;
    --out) OUT="${2:-}"; shift 2 ;;
    --with-symbols-stub) WITH_SYMBOLS=1; shift ;;
    -h|--help) usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -n "$OS" && -n "$VERSION" && -n "$BIN" ]] || usage
[[ -f "$BIN" ]] || { echo "binary not found: $BIN" >&2; exit 1; }
[[ -f "$ROOT/LICENSE" ]] || { echo "LICENSE missing at $ROOT/LICENSE" >&2; exit 1; }

case "$OS" in
  linux|windows) ;;
  *) echo "--os must be linux or windows" >&2; exit 1 ;;
esac

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/antinomia-pkg.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

write_install_txt() {
  local dest="$1" plat="$2"
  cat >"$dest" <<EOF
antinomia ${VERSION} (${plat}-x86_64)

Quick install
-------------
Linux:   ./install.sh   (copies to ~/.local/bin)
Windows: .\\install.ps1 (copies to %LOCALAPPDATA%\\antinomia)

Or run the binary from this directory:

  ./antinomia version
  antinomia.exe version

See docs/release-checklist.md in the source tree for a reproducible release.
Schema: antinomia/1
EOF
}

write_linux_install() {
  cat >"$1" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${DESTDIR:-$HOME/.local/bin}"
mkdir -p "$DEST"
install -m 0755 "$HERE/antinomia" "$DEST/antinomia"
echo "installed: $DEST/antinomia"
EOF
  chmod 0755 "$1"
}

write_windows_install() {
  cat >"$1" <<'EOF'
# install.ps1 - copy antinomia.exe into a per-user directory.
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $env:LOCALAPPDATA "antinomia"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Force (Join-Path $here "antinomia.exe") (Join-Path $dest "antinomia.exe")
Write-Host "installed: $(Join-Path $dest 'antinomia.exe')"
Write-Host "Add that directory to PATH if you want a bare 'antinomia' command."
EOF
}

write_symbols_readme() {
  local dest="$1" plat="$2"
  cat >"$dest" <<EOF
antinomia ${VERSION} symbols package (STUB) — ${plat}-x86_64

This archive is a placeholder for a future optional symbols package.

Planned contents (not yet produced by CI):
  Linux:   DWARF / .debug detached from the clang-release binary
  Windows: PDB alongside the clang-cl-release static-CRT binary

Until those land, debug with a local clang-debug / clang-cl-debug build, or keep
unstripped release binaries from your own cmake --build.

Do not expect stack unwind or source mapping from this stub alone.
EOF
}

write_sha256sums() {
  local f
  : >"$OUT/SHA256SUMS"
  # Stable order for reproducible checklists.
  while IFS= read -r f; do
    [[ -f "$f" ]] || continue
    if command -v sha256sum >/dev/null 2>&1; then
      (cd "$OUT" && sha256sum "$(basename "$f")") >>"$OUT/SHA256SUMS"
    else
      (cd "$OUT" && shasum -a 256 "$(basename "$f")") >>"$OUT/SHA256SUMS"
    fi
  done < <(find "$OUT" -maxdepth 1 -type f ! -name SHA256SUMS | sort)
}

PKG_ROOT="${STAGE}/antinomia-${VERSION}-${OS}-x86_64"
mkdir -p "$PKG_ROOT"
cp "$ROOT/LICENSE" "$PKG_ROOT/LICENSE"
write_install_txt "$PKG_ROOT/INSTALL.txt" "$OS"

if [[ "$OS" == "linux" ]]; then
  install -m 0755 "$BIN" "$PKG_ROOT/antinomia"
  write_linux_install "$PKG_ROOT/install.sh"
  BARE="$OUT/antinomia-${VERSION}-linux-x86_64"
  install -m 0755 "$BIN" "$BARE"
  ARCHIVE="$OUT/antinomia-${VERSION}-linux-x86_64.tar.gz"
  tar -C "$STAGE" -czf "$ARCHIVE" "$(basename "$PKG_ROOT")"
  if [[ "$WITH_SYMBOLS" -eq 1 ]]; then
    SYM_ROOT="${STAGE}/antinomia-${VERSION}-linux-x86_64-symbols"
    mkdir -p "$SYM_ROOT/debug"
    write_symbols_readme "$SYM_ROOT/README-SYMBOLS.txt" "linux"
    # Placeholder path reserved for future .debug / DWARF dump.
    : >"$SYM_ROOT/debug/.stub"
    SYM_ARCHIVE="$OUT/antinomia-${VERSION}-linux-x86_64-symbols.tar.gz"
    tar -C "$STAGE" -czf "$SYM_ARCHIVE" "$(basename "$SYM_ROOT")"
  fi
else
  cp "$BIN" "$PKG_ROOT/antinomia.exe"
  write_windows_install "$PKG_ROOT/install.ps1"
  BARE="$OUT/antinomia-${VERSION}-windows-x86_64.exe"
  cp "$BIN" "$BARE"
  ARCHIVE="$OUT/antinomia-${VERSION}-windows-x86_64.zip"
  zip_dir() {
    local src_dir="$1" dest_zip="$2"
    if command -v zip >/dev/null 2>&1; then
      (cd "$(dirname "$src_dir")" && zip -qr "$dest_zip" "$(basename "$src_dir")")
      return
    fi
    # GitHub windows-2022 bash may lack zip; fall back to PowerShell.
    if command -v powershell.exe >/dev/null 2>&1; then
      powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "$src_dir" 2>/dev/null || echo "$src_dir")' -DestinationPath '$(cygpath -w "$dest_zip" 2>/dev/null || echo "$dest_zip")' -Force"
      return
    fi
    echo "zip or powershell.exe is required to package Windows archives" >&2
    exit 1
  }
  zip_dir "$PKG_ROOT" "$ARCHIVE"
  if [[ "$WITH_SYMBOLS" -eq 1 ]]; then
    SYM_ROOT="${STAGE}/antinomia-${VERSION}-windows-x86_64-symbols"
    mkdir -p "$SYM_ROOT/pdb"
    write_symbols_readme "$SYM_ROOT/README-SYMBOLS.txt" "windows"
    : >"$SYM_ROOT/pdb/.stub"
    SYM_ARCHIVE="$OUT/antinomia-${VERSION}-windows-x86_64-symbols.zip"
    zip_dir "$SYM_ROOT" "$SYM_ARCHIVE"
  fi
fi

write_sha256sums
echo "packaged ${OS} ${VERSION} -> ${OUT}" >&2
ls -la "$OUT" >&2
