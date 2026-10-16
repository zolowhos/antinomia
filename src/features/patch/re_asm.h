// re_asm.h - minimal x86-64 inline assembler for debugger patches.
// Module: feature/patch (C11).
// Owns: mnemonic text to machine bytes at a known virtual address.
// Depends: re_err. Complements the native re_disasm_x64 decoder.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "utils/sys/re_err.h"

#define RE_ASM_MAX_BYTES 15
#define RE_ASM_TEXT_MAX 96

/* Assemble one instruction. addr is the VA used for relative branches.
 * Text forms: "nop", "ret", "mov rax, rbx", "jmp 0x401000", "db 90 cc". */
re_err_code_t re_asm_x64(uint64_t addr, const char *text, uint8_t *out, size_t cap, size_t *n_out);

#ifdef __cplusplus
}
#endif
