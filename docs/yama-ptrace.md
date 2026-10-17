# yama-ptrace.md - Linux Yama / attach FAQ for antinomia.
# Module: docs (Markdown).
# Owns: ptrace_scope FAQ, launch vs attach, privilege JSON, CI guidance.
# Depends: Linux backend attach/launch; companion to docs/backend-caps.md.

# Linux Yama / attach FAQ

antinomia drives inferiors through the Linux `ptrace` API (`PTRACE_TRACEME` on
launch, `PTRACE_SEIZE` / `PTRACE_ATTACH` on attach). The kernel Yama LSM can
deny attach even when the binary is otherwise correct. Windows attach uses
`SeDebugPrivilege` — see the short note at the end.

## Quick answers

### Why does `attach <pid>` fail with privilege / EPERM?

Yama `ptrace_scope` is almost always the cause on modern distros. Read it:

```console
$ cat /proc/sys/kernel/yama/ptrace_scope
```

| Value | Meaning for antinomia |
|---|---|
| `0` | Classic: same-uid attach allowed |
| `1` | Restricted (common default): attach only to descendants, or with `CAP_SYS_PTRACE` / root |
| `2` / `3` | Admin-only / disabled — attach needs elevated privilege |

**Prefer `init` / `launch` for fixtures and CI.** Same-uid launch works under the
usual `ptrace_scope=1` default because the child calls `PTRACE_TRACEME` before
`exec`.

### Launch works but attach does not — is that a bug?

Usually no. Launch and attach hit different Yama rules. Use launch when you
control the binary; only attach when you must join an already-running process.

### How do I temporarily allow same-uid attach on a throwaway VM?

```console
$ echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

Do **not** bake this into product images or CI without review. Prefer launch in
automated tests.

### Should I setuid or setcap the debugger?

**No** for day-to-day developer binaries. Avoid `setuid` and avoid casually
`setcap cap_sys_ptrace+ep` on workstation builds. Packaging notes may mention
capabilities later; never recommend setuid in docs or scripts.

### What does the JSON error look like?

When attach (or launch) is denied, antinomia returns `RE_E_PRIVILEGE` under
schema `antinomia/1`:

| Field | Example | Meaning |
|---|---|---|
| `error.code` | `"privilege"` | Stable slug for agents |
| `error.kind` | `"attach"` / `"launch"` / `"access"` | Which debug op failed |
| `error.platform` | `"linux"` / `"windows"` | Host OS of the denial |
| `error.hint` | `"/proc/sys/kernel/yama/ptrace_scope"` | Stable remediation token |
| `error.message` | prose | Human-readable detail |

Stderr may also name `/proc/sys/kernel/yama/ptrace_scope`. Agents should key off
`error.code` + `error.hint`, not scrape prose.

### Does CI need a custom Yama setting?

No for the default Linux PR matrix. Headless fixture tests use **launch**
(entry break → regs → step → detach) and stay green on `ptrace_scope=1`.
Attach-heavy local tests are optional; if they fail with EPERM/EACCES, fall
back to launch or use a disposable VM with scope `0`.

### What about AppArmor / SELinux?

Distro MAC frameworks can add further denials. If Yama is `0` and attach still
fails, check `dmesg` / audit logs for AppArmor or SELinux AVCs. antinomia does
not ship policy profiles; document host policy in your environment.

### Can agents detect attach support before trying?

Yes. Query `caps` — `attach` is advertised when the live backend supports it
([backend-caps.md](backend-caps.md)). A true `attach` bit does **not** bypass
Yama; privilege denials still return `RE_E_PRIVILEGE`.

## Launch vs attach (summary)

| Path | Command | Yama `1` typical result |
|---|---|---|
| Launch | `init` / `launch` | Works (same-uid `PTRACE_TRACEME`) |
| Attach descendant | `attach <child>` | Often works |
| Attach unrelated same-uid | `attach <pid>` | Denied without `CAP_SYS_PTRACE` |
| Cross-uid | `attach` | Needs capability / root |

## Windows note

Windows Debug API attach may require `SeDebugPrivilege`. Privilege denials use
the same `RE_E_PRIVILEGE` shape with `error.hint` = `"SeDebugPrivilege"` and
`error.platform` = `"windows"`. CI enables the Windows live step/regs path only
when the backend exposes those ops.

## Related

- [backend-caps.md](backend-caps.md)
- [commands.md](commands.md) — `init`, `attach`, `caps`
- [x64dbg-migration.md](x64dbg-migration.md)
