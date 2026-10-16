# bp-fidelity.md - hardware and memory breakpoint fidelity limits.
# Module: docs (Markdown).
# Owns: caps.hw_bp / caps.mem_bp semantics and platform notes for Phase 2.
# Depends: none. Companion to bph/bpm/bpdll commands.

# Breakpoint fidelity limits (Phase 2)

Schema and capability bits use `caps.hw_bp` / `caps.mem_bp` / `caps.load_bp`
(see `status` and `caps` commands). This note documents what those bits mean today.

## Hardware breakpoints (`bph` / `bphw`)

- **Slots:** four debug registers (DR0–DR3) on x86_64.
- **Linux:** `PTRACE_POKEUSER` / `PEEKUSER` on `u_debugreg[]`.
- **Windows:** `CONTEXT_DEBUG_REGISTERS` via `Get/SetThreadContext`.
- **Sizes:** 1, 2, 4, or 8 bytes.
- **Access:** `x` (execute), `w` (write), `rw` (read/write) — DR7 encoding.

## Memory breakpoints (`bpm`)

**Fidelity limit:** memory watches are **DR-backed only**. There is no page-guard
(`PAGE_GUARD`) or `mprotect` whole-page trap yet.

| Wanted | Current behavior |
|---|---|
| Watch 1/2/4/8-byte location | Supported via a hardware watchpoint slot |
| Watch arbitrary length / whole page | **Not supported** — returns usage error |
| Execute “memory BP” | Use `bph` with access `x` instead |

So `caps.mem_bp` means “memory watches available through HW DRs”, not full
x64dbg page-guard semantics. Agents should check `caps.mem_bp` and honor length
limits in this document.

## Module-load breakpoints (`bpdll`)

- **Windows:** filters `LOAD_DLL_DEBUG_EVENT` by path substring.
- **Linux:** plants a software BP on `r_debug.r_brk` (ld.so rendezvous via
  `AT_PHDR` + `DT_DEBUG`). Hits are reported as `load_module` events.

Clear with `bpdllc <name>`. List entries appear in `bplist` with `"kind":"load"`.
