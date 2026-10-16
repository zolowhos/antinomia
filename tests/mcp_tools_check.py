#!/usr/bin/env python3
# mcp_tools_check.py - validate MCP stdio frames for tools/list and tools/call.
# Module: test (Python 3).
# Owns: schema antinomia/1 and Phase 1-2 tool surface checks without CMake lists.
# Depends: stdlib json only. Reads a file path (not argv payload) so Windows
# CreateProcess command-line length limits do not truncate tools/list (~40KB).
import json
import sys

REQUIRED = [
    "status",
    "init",
    "launch",
    "attach",
    "detach",
    "kill",
    "wait_event",
    "run",
    "g",
    "cont",
    "step",
    "sti",
    "t",
    "sto",
    "p",
    "pause",
    "bp",
    "bc",
    "be",
    "bd",
    "bplist",
    "bph",
    "bpm",
    "bpdll",
    "regs",
    "set_regs",
    "d",
    "u",
    "mem_read",
    "mem_write",
    "threads",
    "modules",
    "maps",
    "lm",
    "bt",
    "sym",
    "expr",
    "var",
    "watch",
    "graph",
    "find",
    "search",
    "caps",
    "exlist",
    "tracestart",
    "trace.query",
    "script",
    "note",
    "notes",
    "log",
]


def frames(raw: str):
    out = []
    for line in raw.replace("\r\n", "\n").split("\n"):
        line = line.strip()
        if line:
            out.append(json.loads(line))
    return out


def check_list(raw: str) -> None:
    fr = frames(raw)
    if len(fr) < 2:
        raise SystemExit(f"expected initialize and tools/list, got {len(fr)}")
    init = fr[0]["result"]
    if init.get("protocolVersion") != "2025-06-18":
        raise SystemExit(f"bad protocolVersion: {init.get('protocolVersion')}")
    tools = {t["name"]: t for t in fr[1]["result"]["tools"]}
    for want in REQUIRED:
        if want not in tools:
            raise SystemExit(f"tools/list missing {want}")
    attach = tools["attach"]["inputSchema"]
    if "pid" not in attach.get("required", []):
        raise SystemExit("attach inputSchema must require pid")
    init_schema = tools["init"]["inputSchema"]
    if "path" not in init_schema.get("required", []):
        raise SystemExit("init inputSchema must require path")
    bp = tools["bp"]["inputSchema"]["properties"]
    if bp.get("addr", {}).get("type") != "string":
        raise SystemExit("bp inputSchema must document addr")
    print("ok")


def check_call(raw: str) -> None:
    fr = frames(raw)
    if len(fr) < 4:
        raise SystemExit(f"expected initialize + 3 tools/call, got {len(fr)}")
    status = fr[1]["result"]["structuredContent"]
    if status.get("schema") != "antinomia/1" or status.get("tool") != "status":
        raise SystemExit(f"bad status payload: {status}")
    missing = fr[2]["result"]
    sc = missing["structuredContent"]
    if sc.get("schema") != "antinomia/1":
        raise SystemExit(f"missing-tool lacks schema: {sc}")
    if sc.get("error", {}).get("code") != "notfound":
        raise SystemExit(f"expected notfound, got {sc.get('error')}")
    if not missing.get("isError"):
        raise SystemExit("missing-tool should set isError")
    attach = fr[3]["result"]["structuredContent"]
    if attach.get("schema") != "antinomia/1":
        raise SystemExit(f"attach error lacks schema: {attach}")
    if attach.get("error", {}).get("code") != "usage":
        raise SystemExit(f"expected attach usage, got {attach.get('error')}")
    print("ok")


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: mcp_tools_check.py list|call <capture-file>")
    mode, path = sys.argv[1], sys.argv[2]
    with open(path, "r", encoding="utf-8", newline="") as f:
        raw = f.read()
    if mode == "list":
        check_list(raw)
    elif mode == "call":
        check_call(raw)
    else:
        raise SystemExit(f"unknown mode {mode}")


if __name__ == "__main__":
    main()
