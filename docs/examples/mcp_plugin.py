#!/usr/bin/env python3
# mcp_plugin.py - process-external MCP-as-plugin example for antinomia.
# Module: docs/examples (Python 3).
# Owns: minimal stdio MCP client calling privacy + status.
# Depends: antinomia mcp (schema antinomia/1). No network.
"""Example plugin: drive antinomia over MCP stdio (Phase 3 freeze)."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from typing import Any, Dict, List, Optional


def frames(raw: str) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    for line in raw.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line:
            out.append(json.loads(line))
    return out


def sc_of(fr: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    r = fr.get("result")
    if not isinstance(r, dict):
        return None
    sc = r.get("structuredContent")
    return sc if isinstance(sc, dict) else None


def main() -> int:
    ap = argparse.ArgumentParser(description="MCP-as-plugin example for antinomia")
    ap.add_argument(
        "--antinomia",
        default="antinomia",
        help="path to antinomia binary (default: antinomia on PATH)",
    )
    args = ap.parse_args()
    reqs = [
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-06-18",
                "capabilities": {},
                "clientInfo": {"name": "antinomia-mcp-plugin-example", "version": "0.1.0"},
            },
        },
        {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "privacy", "arguments": {"seed": 42}},
        },
        {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {"name": "status", "arguments": {}},
        },
    ]
    payload = ("\n".join(json.dumps(r, separators=(",", ":")) for r in reqs) + "\n").encode()
    with tempfile.NamedTemporaryFile("wb", delete=False) as tf:
        tf.write(payload)
        in_path = tf.name
    out_path = in_path + ".out"
    err_path = out_path + ".err"
    try:
        with open(in_path, "rb") as fin, open(out_path, "wb") as fout, open(
            err_path, "wb"
        ) as ferr:
            proc = subprocess.run(
                [args.antinomia, "mcp"],
                stdin=fin,
                stdout=fout,
                stderr=ferr,
                timeout=60,
            )
        raw = open(out_path, "r", encoding="utf-8", newline="").read()
        if proc.returncode != 0:
            err = open(err_path, "r", encoding="utf-8", errors="replace").read()
            print(f"mcp exited {proc.returncode}: {err}", file=sys.stderr)
            return 1
        by_id: Dict[int, Dict[str, Any]] = {}
        for fr in frames(raw):
            if "id" in fr:
                by_id[int(fr["id"])] = fr
        listed = by_id.get(2, {}).get("result", {})
        names = {t.get("name") for t in listed.get("tools", []) if isinstance(t, dict)}
        if "privacy" not in names:
            print("privacy tool missing from tools/list", file=sys.stderr)
            return 1
        priv = sc_of(by_id.get(3, {}))
        if not priv or priv.get("schema") != "antinomia/1":
            print(f"privacy confirm failed: {by_id.get(3)}", file=sys.stderr)
            return 1
        if priv.get("telemetry") is not False or priv.get("seed") != 42:
            print(f"privacy fields wrong: {priv}", file=sys.stderr)
            return 1
        st = sc_of(by_id.get(4, {}))
        if not st or st.get("schema") != "antinomia/1":
            print(f"status failed: {by_id.get(4)}", file=sys.stderr)
            return 1
        print(
            f"plugin ok telemetry={priv.get('telemetry')} seed={priv.get('seed')} "
            f"backend={st.get('backend')}",
            file=sys.stderr,
        )
        return 0
    except Exception as exc:  # noqa: BLE001 - example script surface
        print(f"mcp_plugin failed: {exc}", file=sys.stderr)
        return 1
    finally:
        for p in (in_path, out_path, err_path):
            try:
                import os

                os.unlink(p)
            except OSError:
                pass


if __name__ == "__main__":
    sys.exit(main())
