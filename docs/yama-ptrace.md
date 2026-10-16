# yama-ptrace.md - Linux Yama and ptrace attach requirements for antinomia.
# Module: docs (Markdown).
# Owns: attach and CI guidance for ptrace_scope and CAP_SYS_PTRACE.
# Depends: none. Companion to Phase 1 process-control backends.

antinomia drives inferiors through the Linux `ptrace` API (`PTRACE_TRACEME` on
launch, `PTRACE_SEIZE` / `PTRACE_ATTACH` on attach). The kernel Yama LSM can
deny attach even when the binary is otherwise correct.

## What to check

| Setting | Path | Typical meaning |
|---|---|---|
| Yama scope | `/proc/sys/kernel/yama/ptrace_scope` | 0 = classic; 1 = restricted attach (default on many distros); 2/3 = admin-only |
| Capability | `CAP_SYS_PTRACE` | Needed for some cross-uid attach cases; not required for same-uid launch |

Read the current value:

```console
$ cat /proc/sys/kernel/yama/ptrace_scope
```

## Launch vs attach

- **Launch** (`init` / `launch`): the child calls `PTRACE_TRACEME` before `exec`.
  Same-uid launch works under the common `ptrace_scope=1` default.
- **Attach** (`attach <pid>`): subject to Yama. Scope `1` allows attaching only
  to descendants (or with `CAP_SYS_PTRACE` / root). Scope `0` restores classic
  same-uid attach.

When attach (or launch) is denied, antinomia returns `RE_E_PRIVILEGE` with
structured JSON under schema `antinomia/1`:

| Field | Example | Meaning |
|---|---|---|
| `error.code` | `"privilege"` | Stable slug for agents |
| `error.kind` | `"attach"` / `"launch"` / `"access"` | Which debug op failed |
| `error.platform` | `"linux"` / `"windows"` | Host OS of the denial |
| `error.hint` | `"/proc/sys/kernel/yama/ptrace_scope"` | Stable remediation token |
| `error.message` | prose | Human-readable detail |

Stderr may also name `/proc/sys/kernel/yama/ptrace_scope` (never setuid the
debugger). On Windows, `error.hint` is `"SeDebugPrivilege"`.

## CI and local tests

Linux PR CI runs headless fixture tests (launch -> entry break -> regs -> step ->
detach) without a TTY. Those tests use **launch**, so they stay green on default
Yama settings.

If you enable attach-heavy local tests and see EPERM/EACCES:

1. Prefer launching the fixture under the debugger instead of attaching.
2. For a throwaway VM only: `echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope`
   (do not bake this into the product or CI images without review).
3. Avoid `setuid` / `setcap cap_sys_ptrace+ep` on the developer workstation binary;
   document a capability note only when packaging requires it.

## Windows note

Windows Debug API attach uses `SeDebugPrivilege` for some targets. The Windows
integration path for step/regs is enabled in CI only when the backend exposes
those ops (see `debug_launch_bp_step_detach` test properties).
