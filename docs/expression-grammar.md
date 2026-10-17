# expression-grammar.md - C-like debugger expression language.
# Module: docs (Markdown).
# Owns: grammar, operators, atoms, and BP/watch evaluation notes.
# Depends: src/features/expr/*; commands expr/?/var/watch/bpc/*.

# Expression grammar

antinomia evaluates C-like expressions against the live session for:

- `expr` / `?` (CLI / MCP / TUI bar)
- Breakpoint conditions (`bpc`, `bplogc`, `bpcmdc`)
- Watch expressions (`watch` / `wa`)
- Address arguments that accept expressions (`bp`, `bc`, …)

Implementation: `src/features/expr/` (`re_expr_eval`, `re_expr_eval_u64`).
Results are signed 64-bit; address helpers reinterpret the bits as `uint64_t`.

## Atoms

| Form | Meaning |
|---|---|
| `123`, `0xdead` | Decimal or hex integer literal |
| `rax` … `r15`, `rip`, `rflags` / `eflags` | Current-thread GPRs (case-insensitive) |
| `$name` | Session `$variable` (`var` get/set) |
| `[expr]` | Dereference 8 bytes (little-endian qword) at `expr` |
| `byte:[expr]` | 1-byte load (`uint8` / `u8` synonyms) |
| `word:[expr]` | 2-byte load (`uint16` / `u16`) |
| `dword:[expr]` | 4-byte load (`uint32` / `u32`) |
| `qword:[expr]` | 8-byte load (`uint64` / `u64` / `ptr`) |
| `module:symbol` | Resolve via PE export / ELF dynsym (then PDB/DWARF when wired) |
| `(expr)` | Grouping |

Unknown identifiers and missing `$vars` fail the expression (`RE_E_MALFORMED`
with a short error string). Memory reads that fail the backend also fail the
expression.

## Unary operators

| Op | Meaning |
|---|---|
| `+x` | Identity |
| `-x` | Negate |
| `!x` | Logical not → `0` or `1` |
| `~x` | Bitwise not |

## Binary operators (low → high precedence chains)

Evaluation is left-associative within each level. Precedence (lowest first):

| Level | Operators |
|---|---|
| Logical or | `\|\|` |
| Logical and | `&&` |
| Bitwise or | `\|` |
| Bitwise xor | `^` |
| Bitwise and | `&` |
| Equality | `==` `!=` |
| Relational | `<` `>` `<=` `>=` |
| Shift | `<<` `>>` (shift count 0..63; logical on bits) |
| Add | `+` `-` |
| Mul | `*` `/` `%` (division by zero errors) |

Logical `&&` / `\|\|` always evaluate both sides (no C short-circuit skip of
side effects — there are no assignment side effects in this grammar).

## Examples

```text
? rax + 8
? dword:[rsp]
? [rbp-0x10] == 0
? ntdll:NtCreateFile
expr $hitcount + 1
bpc 0x401000 rax==0 && dword:[rsp+8]!=0
```

## Not in scope

- Assignment inside expressions (`rax=1` is not an expr; use `set_regs` / `var`)
- Floating point, SIMD registers, segment bases as first-class atoms
- Function calls / casts beyond the typed `width:[addr]` loads
- Step-out / call-frame sugar

## Related

- [commands.md](commands.md) — `expr`, `var`, `watch*`, `bpc*`
- [x64dbg-migration.md](x64dbg-migration.md) — expression differences vs x64dbg
