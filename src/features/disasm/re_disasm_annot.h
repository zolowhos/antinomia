// re_disasm_annot.h - string and module comments for disasm lines.
// Module: feature (C11).
// Owns: comment fill from modules, printable strings, and user DB notes.
// Depends: re_disasm, re_dbg session types. No Capstone headers.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "features/debug/re_dbg.h"
#include "features/disasm/re_disasm.h"

// Annotate one insn: module+rva and/or quoted string into insn->comment.
void re_disasm_annot_one(re_dbg_session_t *s, re_disasm_insn_t *insn);

// Annotate a window of instructions in place.
void re_disasm_annot_many(re_dbg_session_t *s, re_disasm_insn_t *lines, size_t n);

#ifdef __cplusplus
}
#endif
