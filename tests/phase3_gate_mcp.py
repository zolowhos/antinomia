#!/usr/bin/env python3
# phase3_gate_mcp.py - Phase 3 gate: pure MCP stdio agent debug loop.
# Module: test (Python 3).
# Owns: init -> bp -> run -> state -> sti loop -> detach over tools/call.
# Depends: stdlib only. Inferior path is argv; no TUI.
"""Drive antinomia mcp with the Phase 3 agent-path sequence (no TUI).

Usage:
  phase3_gate_mcp.py <antinomia> <inferior> [capture-out]
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from typing import Any


def call(rid: int, name: str, args: dict[str, Any] | None = None) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": rid,
        "method": "tools/call",
        "params": {"name": name, "arguments": args or {}},
    }


def build_requests(inferior: str) -> list[dict[str, Any]]:
    # Mirror scripts/phase3_state_demo.re.in timing: drain loader stops, pause
    # before sti so regs/mem stay stable across THREAD_CREATE auto-continues.
    return [
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "phase3-gate", "version": "1"},
            },
        },
        {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        call(2, "init", {"path": inferior}),
        call(3, "wait_event", {"timeout_ms": "5000"}),
        call(4, "wait_event", {"timeout_ms": "5000"}),
        call(5, "bp", {"addr": "loop_inferior:antinomia_touch"}),
        call(6, "run", {}),
        call(7, "wait_event", {"timeout_ms": "5000"}),
        call(8, "wait_event", {"timeout_ms": "5000"}),
        call(9, "pause", {}),
        call(10, "wait_event", {"timeout_ms": "2000"}),
        call(11, "state", {"limit": 4}),
        call(12, "sti", {}),
        call(13, "wait_event", {"timeout_ms": "5000"}),
        call(14, "sti", {}),
        call(15, "wait_event", {"timeout_ms": "5000"}),
        call(16, "sti", {}),
        call(17, "wait_event", {"timeout_ms": "5000"}),
        call(18, "state", {"limit": 4}),
        call(19, "detach", {}),
    ]


def frames(raw: str) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for line in raw.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line:
            out.append(json.loads(line))
    return out


def sc_of(fr: dict[str, Any]) -> dict[str, Any] | None:
    r = fr.get("result")
    if not isinstance(r, dict):
        return None
    sc = r.get("structuredContent")
    return sc if isinstance(sc, dict) else None


def require_ok(by_id: dict[int, dict[str, Any]], rid: int, tool: str) -> dict[str, Any]:
    fr = by_id.get(rid)
    if fr is None:
        raise SystemExit(f"missing response id={rid} for {tool}")
    if "error" in fr:
        raise SystemExit(f"rpc error id={rid} {tool}: {fr['error']}")
    sc = sc_of(fr)
    if sc is None:
        raise SystemExit(f"no structuredContent id={rid} {tool}")
    if sc.get("tool") != tool and sc.get("cmd") != tool:
        raise SystemExit(f"id={rid} expected tool={tool}, got {sc.get('tool')}")
    if sc.get("ok") is False or fr.get("result", {}).get("isError"):
        raise SystemExit(f"id={rid} {tool} failed: {sc.get('error')}")
    return sc


def check(raw: str) -> None:
    frs = frames(raw)
    by_id: dict[int, dict[str, Any]] = {}
    for fr in frs:
        if "id" in fr:
            by_id[int(fr["id"])] = fr
    init = by_id.get(1, {}).get("result", {})
    if init.get("protocolVersion") != "2025-06-18":
        raise SystemExit(f"bad initialize: {init.get('protocolVersion')}")
    require_ok(by_id, 2, "init")
    require_ok(by_id, 5, "bp")
    require_ok(by_id, 6, "run")
    st1 = require_ok(by_id, 11, "state")
    if "breakpoints" not in st1 or "disasm" not in st1:
        raise SystemExit(f"state snapshot missing panes: {list(st1.keys())}")
    require_ok(by_id, 12, "sti")
    require_ok(by_id, 14, "sti")
    require_ok(by_id, 16, "sti")
    require_ok(by_id, 18, "state")
    require_ok(by_id, 19, "detach")
    print("ok")


def main() -> None:
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: phase3_gate_mcp.py <antinomia> <inferior> [capture-out]")
    ant, inferior = sys.argv[1], sys.argv[2]
    capture = sys.argv[3] if len(sys.argv) == 4 else None
    if not os.path.isfile(ant):
        raise SystemExit(f"antinomia not found: {ant}")
    if not os.path.isfile(inferior):
        raise SystemExit(f"inferior not found: {inferior}")
    reqs = build_requests(os.path.abspath(inferior))
    payload = ("\n".join(json.dumps(r, separators=(",", ":")) for r in reqs) + "\n").encode()
    with tempfile.NamedTemporaryFile("wb", delete=False) as tf:
        tf.write(payload)
        in_path = tf.name
    out_path = capture or in_path + ".out"
    try:
        err_path = out_path + ".err"
        # Avoid stderr=PIPE deadlock if the child logs while we wait on exit.
        with open(in_path, "rb") as fin, open(out_path, "wb") as fout, open(
            err_path, "wb"
        ) as ferr:
            proc = subprocess.run(
                [ant, "mcp"],
                stdin=fin,
                stdout=fout,
                stderr=ferr,
                timeout=120,
            )
        raw = open(out_path, "r", encoding="utf-8", newline="").read()
        if proc.returncode != 0:
            err = open(err_path, "r", encoding="utf-8", errors="replace").read()
            raise SystemExit(f"mcp exited {proc.returncode}: {err}\n{raw[-2000:]}")
        check(raw)
        try:
            os.unlink(err_path)
        except OSError:
            pass
    finally:
        try:
            os.unlink(in_path)
        except OSError:
            pass
        if capture is None:
            try:
                os.unlink(out_path)
            except OSError:
                pass


if __name__ == "__main__":
    main()
