#!/usr/bin/env python3
# phase3_gate_side_client.py - attach client for Phase 3 parallel-session smoke.
# Module: test (Python 3).
# Owns: tools/call sequence over mcp --attach against side-listen owner.
# Depends: stdlib only. Proves same-session mutation without a TTY TUI.
"""Usage: phase3_gate_side_client.py <antinomia> <endpoint> <inferior> <out> <err>"""
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
    return [
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "phase3-gate-side", "version": "1"},
            },
        },
        {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        call(2, "init", {"path": inferior}),
        call(3, "wait_event", {"timeout_ms": "5000"}),
        call(4, "wait_event", {"timeout_ms": "5000"}),
        call(5, "bp", {"addr": "loop_inferior:antinomia_touch"}),
        call(6, "status", {}),
        call(7, "state", {"limit": 4}),
        call(8, "sti", {}),
        call(9, "wait_event", {"timeout_ms": "5000"}),
        call(10, "detach", {}),
    ]


def frames(raw: str) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for line in raw.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line:
            out.append(json.loads(line))
    return out


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: phase3_gate_side_client.py <antinomia> <endpoint> <inferior> <out> <err>"
        )
    ant, endpoint, inferior, out_path, err_path = sys.argv[1:6]
    reqs = build_requests(os.path.abspath(inferior))
    payload = ("\n".join(json.dumps(r, separators=(",", ":")) for r in reqs) + "\n").encode()
    with tempfile.NamedTemporaryFile("wb", delete=False) as tf:
        tf.write(payload)
        in_path = tf.name
    try:
        # File-backed stderr avoids PIPE fill deadlock while waiting on exit.
        with open(in_path, "rb") as fin, open(out_path, "wb") as fout, open(
            err_path, "wb"
        ) as ferr:
            proc = subprocess.run(
                [ant, "mcp", "--attach", endpoint],
                stdin=fin,
                stdout=fout,
                stderr=ferr,
                timeout=120,
            )
        raw = open(out_path, "r", encoding="utf-8", newline="").read()
        if proc.returncode != 0:
            raise SystemExit(f"attach client exited {proc.returncode}: {raw[-1500:]}")
        by_id: dict[int, dict[str, Any]] = {}
        for fr in frames(raw):
            if "id" in fr:
                by_id[int(fr["id"])] = fr
        if by_id.get(1, {}).get("result", {}).get("protocolVersion") != "2025-06-18":
            raise SystemExit("side attach initialize failed")
        for rid, tool in (
            (2, "init"),
            (5, "bp"),
            (6, "status"),
            (7, "state"),
            (8, "sti"),
            (10, "detach"),
        ):
            fr = by_id.get(rid)
            if fr is None:
                raise SystemExit(f"missing id={rid} {tool}")
            sc = fr.get("result", {}).get("structuredContent")
            if not isinstance(sc, dict):
                raise SystemExit(f"id={rid} {tool} missing structuredContent")
            if sc.get("ok") is False or fr.get("result", {}).get("isError"):
                raise SystemExit(f"id={rid} {tool} failed: {sc.get('error')}")
        print("ok")
    finally:
        try:
            os.unlink(in_path)
        except OSError:
            pass


if __name__ == "__main__":
    main()
