// re_asm_x64.c - compact x86-64 assembler for common patch mnemonics.
// Module: feature/patch (C11).
// Owns: tokenize, encode nop/ret/mov/alu/jcc/call/jmp/push/pop/db.
// Depends: re_asm.h, re_hex, re_str. No Capstone/Keystone.
#include "features/patch/re_asm.h"

#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

#include <string.h>

typedef struct {
    const char *p;
    size_t n;
    size_t i;
} tok_t;

static void skip_ws(tok_t *t) {
    while (t->i < t->n && (t->p[t->i] == ' ' || t->p[t->i] == '\t' || t->p[t->i] == ','))
        t->i++;
}

static bool take_ident(tok_t *t, re_str_t *out) {
    size_t start;
    skip_ws(t);
    start = t->i;
    while (t->i < t->n) {
        char c = t->p[t->i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.')
            t->i++;
        else
            break;
    }
    if (t->i == start)
        return false;
    *out = re_strn(t->p + start, t->i - start);
    return true;
}

static bool parse_u64_tok(tok_t *t, uint64_t *out) {
    size_t start;
    uint64_t base = 10, acc = 0;
    size_t i;
    skip_ws(t);
    start = t->i;
    if (start >= t->n)
        return false;
    if (t->n - start >= 2 && t->p[start] == '0' &&
        (t->p[start + 1] == 'x' || t->p[start + 1] == 'X')) {
        base = 16;
        start += 2;
    }
    i = start;
    if (i >= t->n)
        return false;
    for (; i < t->n; i++) {
        int d = re_hex_val(t->p[i]);
        if (d < 0 || (uint64_t)d >= base)
            break;
        acc = acc * base + (uint64_t)d;
    }
    if (i == start)
        return false;
    t->i = i;
    *out = acc;
    return true;
}

static int reg_index(re_str_t name, int *width) {
    static const char *r64[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                  "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    static const char *r32[16] = {"eax", "ecx", "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
                                  "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
    static const char *r8[16] = {"al",  "cl",  "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
                                 "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
    int i;
    for (i = 0; i < 16; i++) {
        if (re_str_ieq_cstr(name, r64[i])) {
            *width = 64;
            return i;
        }
        if (re_str_ieq_cstr(name, r32[i])) {
            *width = 32;
            return i;
        }
        if (re_str_ieq_cstr(name, r8[i])) {
            *width = 8;
            return i;
        }
    }
    return -1;
}

static bool emit(uint8_t *out, size_t cap, size_t *n, uint8_t b) {
    if (*n >= cap)
        return false;
    out[(*n)++] = b;
    return true;
}

static bool emit_imm(uint8_t *out, size_t cap, size_t *n, uint64_t v, size_t nbytes) {
    size_t k;
    for (k = 0; k < nbytes; k++) {
        if (!emit(out, cap, n, (uint8_t)(v & 0xffu)))
            return false;
        v >>= 8;
    }
    return true;
}

static bool enc_nop_ret(re_str_t mnem, tok_t *t, uint8_t *out, size_t cap, size_t *n) {
    uint64_t imm = 0;
    if (re_str_ieq_cstr(mnem, "nop"))
        return emit(out, cap, n, 0x90);
    if (re_str_ieq_cstr(mnem, "int3"))
        return emit(out, cap, n, 0xCC);
    if (re_str_ieq_cstr(mnem, "ret") || re_str_ieq_cstr(mnem, "retn")) {
        skip_ws(t);
        if (t->i >= t->n)
            return emit(out, cap, n, 0xC3);
        if (!parse_u64_tok(t, &imm) || !emit(out, cap, n, 0xC2))
            return false;
        return emit_imm(out, cap, n, imm, 2);
    }
    return false;
}

static bool enc_push_pop(re_str_t mnem, tok_t *t, uint8_t *out, size_t cap, size_t *n) {
    re_str_t reg;
    int w = 0, idx;
    uint64_t imm = 0;
    bool is_push = re_str_ieq_cstr(mnem, "push");
    bool is_pop = re_str_ieq_cstr(mnem, "pop");
    if (!is_push && !is_pop)
        return false;
    skip_ws(t);
    if (is_push && parse_u64_tok(t, &imm)) {
        if (imm <= 0x7fu) {
            return emit(out, cap, n, 0x6A) && emit(out, cap, n, (uint8_t)imm);
        }
        return emit(out, cap, n, 0x68) && emit_imm(out, cap, n, imm, 4);
    }
    if (!take_ident(t, &reg))
        return false;
    idx = reg_index(reg, &w);
    if (idx < 0 || w != 64)
        return false;
    if (idx >= 8 && !emit(out, cap, n, (uint8_t)(0x41u)))
        return false;
    return emit(out, cap, n, (uint8_t)((is_push ? 0x50u : 0x58u) + (idx & 7)));
}

static bool enc_mov_rr(int dst, int src, int w, uint8_t *out, size_t cap, size_t *n) {
    uint8_t rex = 0x40;
    if (w == 64)
        rex |= 0x08;
    if (src >= 8)
        rex |= 0x04;
    if (dst >= 8)
        rex |= 0x01;
    if (rex != 0x40 || w == 64) {
        if (!emit(out, cap, n, rex))
            return false;
    }
    if (!emit(out, cap, n, 0x89))
        return false;
    return emit(out, cap, n, (uint8_t)(0xC0u | ((src & 7) << 3) | (dst & 7)));
}

static bool enc_mov_imm(int dst, int w, uint64_t imm, uint8_t *out, size_t cap, size_t *n) {
    uint8_t rex = 0x40;
    size_t nbytes;
    if (w == 64)
        rex |= 0x08;
    if (dst >= 8)
        rex |= 0x01;
    if (w == 8) {
        if (rex != 0x40 && !emit(out, cap, n, rex))
            return false;
        return emit(out, cap, n, (uint8_t)(0xB0u + (dst & 7))) && emit(out, cap, n, (uint8_t)imm);
    }
    if (rex != 0x40 || w == 64) {
        if (!emit(out, cap, n, rex))
            return false;
    }
    if (!emit(out, cap, n, (uint8_t)(0xB8u + (dst & 7))))
        return false;
    nbytes = w == 64 ? 8u : (w == 16 ? 2u : 4u);
    return emit_imm(out, cap, n, imm, nbytes);
}

static bool enc_xor_rr(int dst, int src, int w, uint8_t *out, size_t cap, size_t *n) {
    uint8_t rex = 0x40;
    if (w == 64)
        rex |= 0x08;
    if (src >= 8)
        rex |= 0x04;
    if (dst >= 8)
        rex |= 0x01;
    if (rex != 0x40 || w == 64) {
        if (!emit(out, cap, n, rex))
            return false;
    }
    if (!emit(out, cap, n, 0x31))
        return false;
    return emit(out, cap, n, (uint8_t)(0xC0u | ((src & 7) << 3) | (dst & 7)));
}

static int alu_reg(re_str_t mnem) {
    if (re_str_ieq_cstr(mnem, "add"))
        return 0;
    if (re_str_ieq_cstr(mnem, "or"))
        return 1;
    if (re_str_ieq_cstr(mnem, "adc"))
        return 2;
    if (re_str_ieq_cstr(mnem, "sbb"))
        return 3;
    if (re_str_ieq_cstr(mnem, "and"))
        return 4;
    if (re_str_ieq_cstr(mnem, "sub"))
        return 5;
    if (re_str_ieq_cstr(mnem, "xor"))
        return 6;
    if (re_str_ieq_cstr(mnem, "cmp"))
        return 7;
    return -1;
}

static bool enc_alu_imm(int op, int dst, int w, uint64_t imm, uint8_t *out, size_t cap, size_t *n) {
    uint8_t rex = 0x40;
    int8_t simm;
    if (w == 64)
        rex |= 0x08;
    if (dst >= 8)
        rex |= 0x01;
    if (rex != 0x40 || w == 64) {
        if (!emit(out, cap, n, rex))
            return false;
    }
    simm = (int8_t)imm;
    if ((int64_t)(int8_t)imm == (int64_t)imm) {
        if (!emit(out, cap, n, 0x83))
            return false;
        if (!emit(out, cap, n, (uint8_t)(0xC0u | (op << 3) | (dst & 7))))
            return false;
        return emit(out, cap, n, (uint8_t)simm);
    }
    if (!emit(out, cap, n, 0x81))
        return false;
    if (!emit(out, cap, n, (uint8_t)(0xC0u | (op << 3) | (dst & 7))))
        return false;
    return emit_imm(out, cap, n, imm, 4);
}

static bool enc_branch(re_str_t mnem, uint64_t addr, tok_t *t, uint8_t *out, size_t cap,
                       size_t *n) {
    uint64_t target = 0;
    int64_t rel;
    uint8_t op8 = 0, op32 = 0;
    if (re_str_ieq_cstr(mnem, "jmp")) {
        op8 = 0xEB;
        op32 = 0xE9;
    } else if (re_str_ieq_cstr(mnem, "call")) {
        op32 = 0xE8;
    } else if (re_str_ieq_cstr(mnem, "je") || re_str_ieq_cstr(mnem, "jz")) {
        op8 = 0x74;
    } else if (re_str_ieq_cstr(mnem, "jne") || re_str_ieq_cstr(mnem, "jnz")) {
        op8 = 0x75;
    } else if (re_str_ieq_cstr(mnem, "ja") || re_str_ieq_cstr(mnem, "jnbe")) {
        op8 = 0x77;
    } else if (re_str_ieq_cstr(mnem, "jb") || re_str_ieq_cstr(mnem, "jnae") ||
               re_str_ieq_cstr(mnem, "jc")) {
        op8 = 0x72;
    } else {
        return false;
    }
    if (!parse_u64_tok(t, &target))
        return false;
    if (op32 && !op8) {
        rel = (int64_t)target - (int64_t)(addr + 5);
        return emit(out, cap, n, op32) && emit_imm(out, cap, n, (uint64_t)rel, 4);
    }
    rel = (int64_t)target - (int64_t)(addr + 2);
    if (rel >= -128 && rel <= 127 && op8)
        return emit(out, cap, n, op8) && emit(out, cap, n, (uint8_t)rel);
    if (op32) {
        rel = (int64_t)target - (int64_t)(addr + 5);
        return emit(out, cap, n, op32) && emit_imm(out, cap, n, (uint64_t)rel, 4);
    }
    return false;
}

static bool parse_hex_byte(tok_t *t, uint8_t *out) {
    size_t start, i, nd = 0;
    uint64_t acc = 0;
    skip_ws(t);
    start = t->i;
    if (start + 1 < t->n && t->p[start] == '0' &&
        (t->p[start + 1] == 'x' || t->p[start + 1] == 'X'))
        start += 2;
    for (i = start; i < t->n && nd < 2; i++) {
        int d = re_hex_val(t->p[i]);
        if (d < 0)
            break;
        acc = (acc << 4) | (uint64_t)d;
        nd++;
    }
    if (nd == 0)
        return false;
    t->i = i;
    *out = (uint8_t)acc;
    return true;
}

static bool enc_db(tok_t *t, uint8_t *out, size_t cap, size_t *n) {
    uint8_t v = 0;
    size_t count = 0;
    skip_ws(t);
    while (t->i < t->n) {
        if (!parse_hex_byte(t, &v))
            return count > 0;
        if (!emit(out, cap, n, v))
            return false;
        count++;
        skip_ws(t);
    }
    return count > 0;
}

static bool enc_mov_alu(re_str_t mnem, tok_t *t, uint8_t *out, size_t cap, size_t *n) {
    re_str_t a, b;
    int wa = 0, wb = 0, ia, ib, op;
    uint64_t imm = 0;
    if (!take_ident(t, &a))
        return false;
    ia = reg_index(a, &wa);
    if (ia < 0)
        return false;
    skip_ws(t);
    if (parse_u64_tok(t, &imm)) {
        if (re_str_ieq_cstr(mnem, "mov"))
            return enc_mov_imm(ia, wa, imm, out, cap, n);
        op = alu_reg(mnem);
        return op >= 0 && enc_alu_imm(op, ia, wa, imm, out, cap, n);
    }
    if (!take_ident(t, &b))
        return false;
    ib = reg_index(b, &wb);
    if (ib < 0 || wa != wb)
        return false;
    if (re_str_ieq_cstr(mnem, "mov"))
        return enc_mov_rr(ia, ib, wa, out, cap, n);
    if (re_str_ieq_cstr(mnem, "xor"))
        return enc_xor_rr(ia, ib, wa, out, cap, n);
    return false;
}

re_err_code_t re_asm_x64(uint64_t addr, const char *text, uint8_t *out, size_t cap, size_t *n_out) {
    tok_t t;
    re_str_t mnem;
    size_t n = 0;
    bool ok = false;
    if (!text || !out || !n_out || cap == 0)
        return RE_E_USAGE;
    t.p = text;
    t.n = re_str(text).n;
    t.i = 0;
    *n_out = 0;
    if (!take_ident(&t, &mnem))
        return RE_E_USAGE;
    if (re_str_ieq_cstr(mnem, "db"))
        ok = enc_db(&t, out, cap, &n);
    else if (enc_nop_ret(mnem, &t, out, cap, &n))
        ok = true;
    else if (enc_push_pop(mnem, &t, out, cap, &n))
        ok = true;
    else if (enc_branch(mnem, addr, &t, out, cap, &n))
        ok = true;
    else if (re_str_ieq_cstr(mnem, "mov") || alu_reg(mnem) >= 0)
        ok = enc_mov_alu(mnem, &t, out, cap, &n);
    if (!ok || n == 0 || n > RE_ASM_MAX_BYTES)
        return RE_E_MALFORMED;
    skip_ws(&t);
    if (t.i < t.n)
        return RE_E_MALFORMED;
    *n_out = n;
    return RE_OK;
}
