# backend-caps.md - dual-OS backend capability matrix.
# Module: docs (Markdown).
# Owns: RE_DBG_CAP_* bits, Linux vs Windows support, caps JSON shape.
# Depends: re_dbg.h; re_dbg_linux.c / re_dbg_win.c; caps command.

# Backend capability matrix

OS debug APIs stay behind the opaque `re_dbg_backend` vtable. Agents and scripts
must not assume Windows-only or Linux-only features: query `caps` (or
`status`) and honour the bits.

## `caps` JSON (`antinomia/1`)

```json
{
  "schema": "antinomia/1",
  "tool": "caps",
  "bits": 4095,
  "hw_slots": 4,
  "sw_bp": true,
  "hw_bp": true,
  "mem_bp": true,
  "load_bp": true,
  "step": true,
  "pause": true,
  "attach": true,
  "modules": true,
  "threads": true,
  "exception_bp": true,
  "signal_pass": true,
  "first_chance": false
}
```

`bits` is the raw `RE_DBG_CAP_*` mask; booleans mirror the same flags for agents
that prefer named fields. `hw_slots` is `4` when hardware DRs are available.

## Flag definitions

| Bit | Name | Meaning |
|---|---|---|
| `1<<0` | `sw_bp` | Software breakpoints (`bp` / int3 style) |
| `1<<1` | `hw_bp` | Hardware execute/watch via DR0–DR3 (`bph`) |
| `1<<2` | `mem_bp` | Memory watches — **DR-backed only** today (`bpm`; see [bp-fidelity.md](bp-fidelity.md)) |
| `1<<3` | `step` | Single-step / step-over |
| `1<<4` | `pause` | Interrupt running inferior |
| `1<<5` | `attach` | Attach by pid (subject to privilege policy) |
| `1<<6` | `modules` | Module enumeration |
| `1<<7` | `threads` | Thread enumeration / switch |
| `1<<8` | `exception_bp` | Exception/signal break policies (`exbp` / `exignore` / `expass`) |
| `1<<9` | `signal_pass` | Linux: deliver signal on continue (`PTRACE_CONT(sig)`) |
| `1<<10` | `first_chance` | Windows: first- vs second-chance exception distinction |
| `1<<11` | `load_bp` | Module-load breakpoints (`bpdll`) |

## Matrix (live backends)

| Capability | Linux (`ptrace`) | Windows (Debug API) |
|---|---|---|
| `sw_bp` | yes | yes |
| `hw_bp` / `hw_slots=4` | yes (`u_debugreg`) | yes (`CONTEXT_DEBUG_REGISTERS`) |
| `mem_bp` (DR-backed) | yes | yes |
| `load_bp` | yes (`r_debug.r_brk`) | yes (`LOAD_DLL_DEBUG_EVENT`) |
| `step` / `pause` / `attach` | yes | yes |
| `modules` / `threads` | yes | yes |
| `exception_bp` | yes (signo) | yes (exception code) |
| `signal_pass` | **yes** | no |
| `first_chance` | no (stops treated first-chance) | **yes** |
| `seh` / `handles` commands | `RE_E_UNSUPPORTED` | yes |
| `signals` / `sigdisp` | yes | `RE_E_UNSUPPORTED` |

Stub backends (wrong-OS link units) advertise `bits=0`, `hw_slots=0` and return
`RE_E_UNSUPPORTED` from vtable ops so CLI/MCP degrade cleanly.

## Privilege / attach notes

| Host | Denial | Hint token |
|---|---|---|
| Linux | Yama `ptrace_scope` / missing `CAP_SYS_PTRACE` | `/proc/sys/kernel/yama/ptrace_scope` |
| Windows | Missing `SeDebugPrivilege` / ACCESS_DENIED | `SeDebugPrivilege` |

Structured privilege errors use `RE_E_PRIVILEGE`. FAQ: [yama-ptrace.md](yama-ptrace.md).

## Related

- [commands.md](commands.md) — `caps`, `bp*`, `ex*`, OS views
- [bp-fidelity.md](bp-fidelity.md) — HW / mem watch limits
- [x64dbg-migration.md](x64dbg-migration.md)
