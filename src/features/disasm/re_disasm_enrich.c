// re_disasm_enrich.c - control-flow and pointer meta for decoded x86-64 insns.
// Module: feature (C11).
// Owns: flags/target/mem fill used by annot and CFG (native Capstone stand-in).
// Depends: re_disasm.h. No OS calls; operates on already-decoded bytes.
#include "features/disasm/re_disasm.h"

#include <string.h>

typedef struct {
    const uint8_t *p;
    size_t n;
    size_t i;
    uint8_t rex_b, rex_x, rex_r, rex_w;
    bool op66;
} x64e_t;

static bool need(const x64e_t *x, size_t k) {
    return x->i + k <= x->n;
}
static uint8_t get(x64e_t *x) {
    return x->p[x->i++];
}
static uint8_t peek(const x64e_t *x) {
    return x->p[x->i];
}

static bool parse_prefixes(x64e_t *x) {
    for (;;) {
        uint8_t b;
        if (!need(x, 1))
            return false;
        b = peek(x);
        if (b == 0x66) {
            x->op66 = true;
            get(x);
        } else if (b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x26 || b == 0x2E ||
                   b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65) {
            get(x);
        } else if ((b & 0xF0u) == 0x40u) {
            x->rex_w = (uint8_t)((b >> 3) & 1u);
            x->rex_r = (uint8_t)((b >> 2) & 1u);
            x->rex_x = (uint8_t)((b >> 1) & 1u);
            x->rex_b = (uint8_t)(b & 1u);
            get(x);
        } else {
            break;
        }
        if (x->i > 14)
            return false;
    }
    return need(x, 1);
}

static bool read_imm(x64e_t *x, size_t nbytes, uint64_t *out) {
    uint64_t v = 0;
    size_t k;
    if (!need(x, nbytes))
        return false;
    for (k = 0; k < nbytes; k++)
        v |= ((uint64_t)get(x)) << (8u * k);
    if (out)
        *out = v;
    return true;
}

static int64_t sext(uint64_t v, size_t nbytes) {
    if (nbytes >= 8)
        return (int64_t)v;
    {
        uint64_t sign = 1ull << ((nbytes * 8u) - 1u);
        uint64_t mask = (sign << 1u) - 1u;
        v &= mask;
        if (v & sign)
            v |= ~mask;
        return (int64_t)v;
    }
}

static bool parse_modrm_mem(x64e_t *x, uint64_t insn_addr, uint64_t *mem_out, uint32_t *flags) {
    uint8_t modrm, mod, rm;
    int32_t disp = 0;
    if (!need(x, 1))
        return false;
    modrm = get(x);
    mod = (uint8_t)(modrm >> 6);
    rm = (uint8_t)(modrm & 7u);
    if (mod != 3 && rm == 4) {
        uint8_t sib;
        if (!need(x, 1))
            return false;
        sib = get(x);
        if (mod == 0 && (sib & 7u) == 5) {
            uint64_t d = 0;
            if (!read_imm(x, 4, &d))
                return false;
            disp = (int32_t)d;
            *mem_out = insn_addr + (uint64_t)x->i + (uint64_t)(int64_t)disp;
            *flags |= RE_DISASM_F_RIPREL;
            return true;
        }
    }
    if (mod == 1) {
        uint64_t d = 0;
        if (!read_imm(x, 1, &d))
            return false;
        disp = (int8_t)d;
    } else if (mod == 2 || (mod == 0 && rm == 5)) {
        uint64_t d = 0;
        if (!read_imm(x, 4, &d))
            return false;
        disp = (int32_t)d;
        if (mod == 0 && rm == 5) {
            *mem_out = insn_addr + (uint64_t)x->i + (uint64_t)(int64_t)disp;
            *flags |= RE_DISASM_F_RIPREL;
            return true;
        }
    }
    (void)disp;
    return true;
}

static bool enrich_cf(x64e_t *x, re_disasm_insn_t *insn, uint8_t op, uint64_t next) {
    uint64_t imm = 0;
    if (op == 0xC3 || op == 0xC2) {
        insn->flags |= RE_DISASM_F_RET;
        if (op == 0xC2)
            (void)read_imm(x, 2, &imm);
        return true;
    }
    if (op == 0xE8 || op == 0xE9) {
        if (!read_imm(x, 4, &imm))
            return true;
        insn->target = next + (uint64_t)sext(imm, 4);
        insn->flags |= (op == 0xE8) ? RE_DISASM_F_CALL : RE_DISASM_F_JMP;
        return true;
    }
    if (op == 0xEB) {
        if (!read_imm(x, 1, &imm))
            return true;
        insn->target = next + (uint64_t)sext(imm, 1);
        insn->flags |= RE_DISASM_F_JMP;
        return true;
    }
    if (op >= 0x70 && op <= 0x7F) {
        if (!read_imm(x, 1, &imm))
            return true;
        insn->target = next + (uint64_t)sext(imm, 1);
        insn->flags |= RE_DISASM_F_JCC;
        return true;
    }
    if (op == 0x0F) {
        uint8_t op2;
        if (!need(x, 1))
            return true;
        op2 = get(x);
        if (op2 >= 0x80 && op2 <= 0x8F && read_imm(x, 4, &imm)) {
            insn->target = next + (uint64_t)sext(imm, 4);
            insn->flags |= RE_DISASM_F_JCC;
        }
        return true;
    }
    return false;
}

static void enrich_memops(x64e_t *x, re_disasm_insn_t *insn, uint8_t op) {
    uint64_t imm = 0;
    if (op >= 0xB8 && op <= 0xBF) {
        size_t nimm = x->rex_w ? 8u : (x->op66 ? 2u : 4u);
        if (!read_imm(x, nimm, &imm))
            return;
        insn->mem = imm;
        insn->flags |= RE_DISASM_F_IMM;
        return;
    }
    if (op == 0x8D || op == 0x8B || op == 0x89 || op == 0xFF) {
        uint8_t modrm, reg;
        if (!need(x, 1))
            return;
        modrm = peek(x);
        reg = (uint8_t)((modrm >> 3) & 7u);
        if (!parse_modrm_mem(x, insn->addr, &insn->mem, &insn->flags))
            return;
        if (op == 0xFF && reg == 2)
            insn->flags |= RE_DISASM_F_CALL;
        else if (op == 0xFF && reg == 4)
            insn->flags |= RE_DISASM_F_JMP;
    }
}

static void enrich_body(x64e_t *x, re_disasm_insn_t *insn) {
    uint8_t op;
    if (!need(x, 1))
        return;
    op = get(x);
    if (enrich_cf(x, insn, op, insn->addr + insn->size))
        return;
    enrich_memops(x, insn, op);
}

void re_disasm_enrich(re_disasm_insn_t *insn) {
    x64e_t x;
    if (!insn || insn->size == 0)
        return;
    insn->flags = 0;
    insn->target = 0;
    insn->mem = 0;
    memset(&x, 0, sizeof(x));
    x.p = insn->bytes;
    x.n = insn->size;
    if (!parse_prefixes(&x))
        return;
    enrich_body(&x, insn);
}
