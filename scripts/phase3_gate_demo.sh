#!/usr/bin/env bash
# phase3_gate_demo.sh - repo demo for the Phase 3 agent / side-attach gate.
# Module: demo (shell).
# Owns: runnable Linux demo of MCP stdio + optional side-listen attach path.
# Depends: built antinomia + loop_inferior (clang-release by default).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${RE_BUILD_DIR:-$ROOT/build/clang-release}"
ANT="${ANTINOMIA:-$BUILD/bin/antinomia}"
INF="${RE_TEST_INFERIOR:-$BUILD/bin/loop_inferior}"
MODE="${1:-mcp}"

if [[ ! -x "$ANT" ]]; then
  echo "antinomia not found at $ANT (build clang-release first)" >&2
  exit 1
fi
if [[ ! -x "$INF" ]]; then
  echo "loop_inferior not found at $INF" >&2
  exit 1
fi

case "$MODE" in
  mcp|agent)
    echo "== Phase 3 gate: MCP stdio agent path (no TUI) =="
    python3 "$ROOT/tests/phase3_gate_mcp.py" "$ANT" "$INF"
    ;;
  side|attach)
    echo "== Phase 3 gate: side-listen + mcp --attach (CI stand-in for TUI) =="
    cmake -DANTINOMIA="$ANT" -DINFERIOR="$INF" \
      -P "$ROOT/tests/run_phase3_gate_side.cmake"
    ;;
  tui)
    cat <<'EOF'
== Interactive TUI + mcp --attach (not runnable headless) ==

Terminal A:
  antinomia gui
  # log pane shows: mcp unix:/tmp/antinomia-mcp-<pid>.sock
  # or: mcp tcp:127.0.0.1:<port>

Terminal B:
  export ANTINOMIA_MCP=unix:/tmp/antinomia-mcp-<pid>.sock
  antinomia mcp --attach
  # then tools/call init / bp / run / state / sti / detach

CI covers the same session ownership model via `mcp --side-listen` +
`mcp --attach` (ctest phase3_gate_side). Live TUI remains a manual demo.
EOF
    ;;
  *)
    echo "usage: $0 [mcp|side|tui]" >&2
    exit 2
    ;;
esac
