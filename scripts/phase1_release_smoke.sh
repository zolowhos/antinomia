#!/usr/bin/env bash
# phase1_release_smoke.sh - Phase 1 workflow from a packaged Linux release archive.
# Module: release/test (shell).
# Owns: extract archive, run init/bp/regs/step/kill via headless on a fixture.
# Depends: tar archive from package_release.sh; ELF fixture (loop_inferior).
set -euo pipefail

ARCHIVE=""
INFERIOR=""
SYM_BP="loop_inferior:antinomia_touch"
SYM_WATCH="loop_inferior:antinomia_watch"

usage() {
  cat >&2 <<'EOF'
Usage: scripts/phase1_release_smoke.sh --archive PATH.tar.gz --inferior PATH

Extracts a linux-x86_64 release archive and runs the Phase 1 loop from that
binary alone (fixture path may be external). See docs/clean-vm.md.
EOF
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --archive) ARCHIVE="${2:-}"; shift 2 ;;
    --inferior) INFERIOR="${2:-}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "unknown arg: $1" >&2; usage ;;
  esac
done

[[ -n "$ARCHIVE" && -n "$INFERIOR" ]] || usage
[[ -f "$ARCHIVE" ]] || { echo "archive not found: $ARCHIVE" >&2; exit 1; }
[[ -f "$INFERIOR" ]] || { echo "inferior not found: $INFERIOR" >&2; exit 1; }

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/antinomia-p1smoke.XXXXXX")"
cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

tar -xzf "$ARCHIVE" -C "$WORKDIR"
BIN="$(find "$WORKDIR" -type f -name antinomia | head -n1)"
[[ -n "$BIN" && -f "$BIN" ]] || {
  echo "antinomia binary missing inside archive" >&2
  exit 1
}
chmod +x "$BIN"

# version prints a short text line including schema antinomia/1 (often stderr).
"$BIN" version >"$WORKDIR/version.txt" 2>&1
grep -q 'antinomia/1' "$WORKDIR/version.txt"
cat "$WORKDIR/version.txt" >&2

SCRIPT="${WORKDIR}/phase1.re"
OUT="${WORKDIR}/phase1.out"
cat >"$SCRIPT" <<EOF
# Phase 1 release-binary smoke (docs/clean-vm.md)
init ${INFERIOR}
wait_event 5000
wait_event 5000
regs
d --len 64
sti
bp ${SYM_BP}
bph ${SYM_WATCH} 4 w
run
wait_event 5000
regs
sti
wait_event 2000
sto
wait_event 2000
kill
EOF

"$BIN" headless "$SCRIPT" >"$OUT"
grep -q '"schema":"antinomia/1"' "$OUT"
grep -q '"tool":"headless"' "$OUT"
grep -q '"exit":0' "$OUT"
grep -q '"failed":0' "$OUT"
grep -q '"cmd":"bp","ok":true' "$OUT"
grep -q '"cmd":"regs","ok":true' "$OUT"
grep -q '"cmd":"sti","ok":true' "$OUT"
grep -q '"cmd":"d","ok":true' "$OUT"
grep -q '"cmd":"kill","ok":true' "$OUT"

echo "phase1_release_smoke: ok archive=$(basename "$ARCHIVE")" >&2
