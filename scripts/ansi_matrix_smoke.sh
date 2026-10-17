#!/usr/bin/env bash
# ansi_matrix_smoke.sh - CI-feasible ANSI / TERM smoke for antinomia.
# Module: scripts (bash).
# Owns: TERM + NO_COLOR checks against privacy JSON and tui unit binary.
# Depends: antinomia binary path as $1; optional re_test_tui as $2.
set -euo pipefail

ANTI="${1:-}"
ANSI_TEST="${2:-}"

if [[ -z "${ANTI}" || ! -x "${ANTI}" ]]; then
  echo "usage: $0 <antinomia> [re_test_ansi]" >&2
  exit 2
fi

echo "== privacy JSON (no SGR expected on stdout) =="
out="$("${ANTI}" privacy)"
printf '%s\n' "${out}" | grep -q '"schema":"antinomia/1"'
printf '%s\n' "${out}" | grep -q '"telemetry":false'
printf '%s\n' "${out}" | grep -q '"network_phone_home":false'
if printf '%s\n' "${out}" | grep -q $'\x1b'; then
  echo "privacy stdout unexpectedly contained ESC" >&2
  exit 1
fi

echo "== privacy --seed round-trip =="
seed_out="$("${ANTI}" privacy --seed 42)"
printf '%s\n' "${seed_out}" | grep -q '"seed_set":true'
printf '%s\n' "${seed_out}" | grep -q '"seed":42'

if [[ -n "${ANSI_TEST}" && -x "${ANSI_TEST}" ]]; then
  echo "== ansi unit under TERM=${TERM:-<unset>} =="
  "${ANSI_TEST}"
fi

echo "ansi_matrix_smoke: ok TERM=${TERM:-<unset>} NO_COLOR=${NO_COLOR-<unset>}"
