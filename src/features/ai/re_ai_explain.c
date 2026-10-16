// re_ai_explain.c - deterministic plain-English explain for one x86-64 insn.
// Module: feature/ai (C11).
// Owns: mnemonic class + summary/detail strings for the explain tool.
// Depends: re_ai.h. No OS calls; operates on already-decoded insn meta.
#include "features/ai/re_ai.h"

#include <stdio.h>
#include <string.h>

static void copy_cap(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void mnemonic(const char *text, char *out, size_t cap) {
    size_t i = 0;
    if (!text) {
        if (cap)
            out[0] = 0;
        return;
    }
    while (text[i] && text[i] != ' ' && text[i] != '\t' && i + 1u < cap) {
        out[i] = text[i];
        i++;
    }
    out[i] = 0;
}

static void sum_cf(const re_disasm_insn_t *in, re_ai_explain_t *out) {
    if (in->flags & RE_DISASM_F_CALL) {
        copy_cap(out->kind, sizeof(out->kind), "call");
        if (in->target)
            snprintf(out->summary, sizeof(out->summary),
                     "Call transfers control to 0x%llx (return address pushed).",
                     (unsigned long long)in->target);
        else
            copy_cap(out->summary, sizeof(out->summary),
                     "Call transfers control; return address is pushed on the stack.");
        return;
    }
    if (in->flags & RE_DISASM_F_RET) {
        copy_cap(out->kind, sizeof(out->kind), "ret");
        copy_cap(out->summary, sizeof(out->summary),
                 "Return pops the stack and resumes at the caller.");
        return;
    }
    if (in->flags & RE_DISASM_F_JCC) {
        copy_cap(out->kind, sizeof(out->kind), "jcc");
        if (in->target)
            snprintf(out->summary, sizeof(out->summary),
                     "Conditional branch may jump to 0x%llx based on flags.",
                     (unsigned long long)in->target);
        else
            copy_cap(out->summary, sizeof(out->summary),
                     "Conditional branch may jump based on CPU flags.");
        return;
    }
    if (in->flags & RE_DISASM_F_JMP) {
        copy_cap(out->kind, sizeof(out->kind), "jmp");
        if (in->target)
            snprintf(out->summary, sizeof(out->summary), "Unconditional jump to 0x%llx.",
                     (unsigned long long)in->target);
        else
            copy_cap(out->summary, sizeof(out->summary),
                     "Unconditional jump to a register or memory target.");
    }
}

static void fill_summary(const re_disasm_insn_t *in, const char *mnem, re_ai_explain_t *out) {
    if (in->flags & (RE_DISASM_F_CALL | RE_DISASM_F_RET | RE_DISASM_F_JCC | RE_DISASM_F_JMP)) {
        sum_cf(in, out);
        return;
    }
    if ((in->flags & RE_DISASM_F_RIPREL) || in->mem) {
        copy_cap(out->kind, sizeof(out->kind), "mem");
        if (in->mem)
            snprintf(out->summary, sizeof(out->summary),
                     "Memory operand references 0x%llx (rip-relative or absolute).",
                     (unsigned long long)in->mem);
        else
            copy_cap(out->summary, sizeof(out->summary), "Instruction uses a memory operand.");
        return;
    }
    copy_cap(out->kind, sizeof(out->kind), "other");
    if (mnem[0])
        snprintf(out->summary, sizeof(out->summary), "Instruction '%s' at 0x%llx.", mnem,
                 (unsigned long long)in->addr);
    else
        snprintf(out->summary, sizeof(out->summary), "Instruction at 0x%llx.",
                 (unsigned long long)in->addr);
}

static void fill_detail(const re_disasm_insn_t *in, re_ai_explain_t *out) {
    char buf[RE_AI_DETAIL_MAX];
    size_t n = 0;
    snprintf(buf, sizeof(buf), "addr=0x%llx size=%u text=\"%s\"", (unsigned long long)in->addr,
             (unsigned)in->size, in->text[0] ? in->text : "?");
    if (in->comment[0]) {
        while (buf[n])
            n++;
        if (n + 12u < sizeof(buf))
            snprintf(buf + n, sizeof(buf) - n, " comment=\"%s\"", in->comment);
    }
    copy_cap(out->detail, sizeof(out->detail), buf);
}

re_err_code_t re_ai_explain_insn(const re_disasm_insn_t *in, re_ai_explain_t *out) {
    char mnem[24];
    if (!in || !out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    mnemonic(in->text, mnem, sizeof(mnem));
    fill_summary(in, mnem, out);
    fill_detail(in, out);
    return RE_OK;
}
