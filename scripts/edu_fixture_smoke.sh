#!/usr/bin/env bash
# edu_fixture_smoke.sh - headless E2E debug sessions on educational fixtures.
# Module: test/edu (shell).
# Owns: launch -> BP -> regs/mem/step/state -> detach|kill for edu dummies.
# Depends: built antinomia + edu_* / breakpoint_me (clang-release by default).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${RE_BUILD_DIR:-$ROOT/build/clang-release}"
ANT="${ANTINOMIA:-$BUILD/bin/antinomia}"
BIN_DIR="${RE_FIXTURE_BIN:-$BUILD/bin}"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/antinomia-edu-smoke.XXXXXX")"
PASS=0
FAIL=0

cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

die() { echo "edu_fixture_smoke: $*" >&2; exit 1; }

[[ -x "$ANT" ]] || die "antinomia not found at $ANT (build clang-release first)"

run_case() {
  local name="$1"
  local script="$2"
  local out="${WORKDIR}/${name}.out"
  shift 2

  echo "== edu case: ${name} ==" >&2
  set +e
  "$ANT" headless "$script" >"$out" 2>"${WORKDIR}/${name}.err"
  local rc=$?
  set -e

  if ! grep -q 'antinomia/1' "$out" || ! grep -q 'headless' "$out"; then
    echo "FAIL ${name}: missing schema/headless (rc=${rc})" >&2
    FAIL=$((FAIL + 1))
    return 0
  fi
  if ! grep -q '"exit":0' "$out" || ! grep -q '"failed":0' "$out"; then
    echo "FAIL ${name}: headless exit/failed non-zero (rc=${rc})" >&2
    sed -n '1,2p' "$out" >&2 || true
    FAIL=$((FAIL + 1))
    return 0
  fi
  local p
  for p in "$@"; do
    if ! grep -q "$p" "$out"; then
      echo "FAIL ${name}: missing pattern: $p" >&2
      FAIL=$((FAIL + 1))
      return 0
    fi
  done
  echo "PASS ${name}" >&2
  PASS=$((PASS + 1))
}

# --- hello then spin: classic launch / BP / regs / mem / step / kill ---
HELLO="${BIN_DIR}/edu_hello_spin"
[[ -x "$HELLO" ]] || die "missing $HELLO"
cat >"${WORKDIR}/hello.re" <<EOF
init ${HELLO}
wait_event 5000
wait_event 5000
bp edu_hello_spin:edu_marker
run
wait_event 5000
regs
d --len 64
sti
wait_event 5000
status
kill
EOF
run_case hello_spin "${WORKDIR}/hello.re" \
  'cmd":"bp","ok":true' \
  'cmd":"regs","ok":true' \
  'cmd":"d","ok":true' \
  'cmd":"sti","ok":true' \
  'cmd":"kill","ok":true'

# --- intentional crash: BP before fault, continue into exception, kill ---
CRASH="${BIN_DIR}/edu_crash"
[[ -x "$CRASH" ]] || die "missing $CRASH"
cat >"${WORKDIR}/crash.re" <<EOF
init ${CRASH}
wait_event 5000
wait_event 5000
bp edu_crash:edu_before_crash
run
wait_event 5000
regs
status
bc edu_crash:edu_before_crash
run
wait_event 5000
status
kill
EOF
run_case crash "${WORKDIR}/crash.re" \
  'cmd":"bp","ok":true' \
  'cmd":"regs","ok":true' \
  'cmd":"kill","ok":true'

# --- multi-thread BP storm lite ---
THREADS="${BIN_DIR}/edu_threads"
[[ -x "$THREADS" ]] || die "missing $THREADS"
cat >"${WORKDIR}/threads.re" <<EOF
init ${THREADS}
wait_event 5000
wait_event 5000
bp edu_threads:edu_worker_hit
run
wait_event 5000
regs
bplist
status
kill
EOF
run_case threads "${WORKDIR}/threads.re" \
  'cmd":"bp","ok":true' \
  'cmd":"regs","ok":true' \
  'cmd":"bplist","ok":true' \
  'cmd":"kill","ok":true'

# --- existing Phase 1 BP target: detach path ---
BPM="${BIN_DIR}/breakpoint_me"
[[ -x "$BPM" ]] || die "missing $BPM"
cat >"${WORKDIR}/bpm.re" <<EOF
init ${BPM}
wait_event 5000
wait_event 5000
bp breakpoint_me:breakpoint_me
run
wait_event 5000
regs
sti
wait_event 5000
detach
EOF
run_case breakpoint_me_detach "${WORKDIR}/bpm.re" \
  'cmd":"bp","ok":true' \
  'cmd":"regs","ok":true' \
  'cmd":"detach","ok":true'

# Best-effort reap of any leftover infinite inferiors from detach.
pkill -f "${BIN_DIR}/breakpoint_me" 2>/dev/null || true
pkill -f "${BIN_DIR}/edu_" 2>/dev/null || true

echo "edu_fixture_smoke: pass=${PASS} fail=${FAIL}" >&2
[[ "$FAIL" -eq 0 && "$PASS" -ge 4 ]]
