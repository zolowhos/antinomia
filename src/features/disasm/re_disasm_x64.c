// re_disasm_x64.c - compact native x86-64 decoder for debugger dumps.
// Module: feature (C11).
// Owns: length decode, common mnemonics, and multi-instruction walks.
// Depends: re_disasm.h. No Capstone; unknown ops render as db.
#include "features/disasm/re_disasm.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *p;
    size_t n;
    size_t i;
    uint8_t rex_b, rex_x, rex_r, rex_w;
    bool op66;
} x64_t;

static const char *kR64[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                               "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
static const char *kR32[16] = {"eax", "ecx", "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
                               "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
static const char *kR16[16] = {"ax",  "cx",  "dx",   "bx",   "sp",   "bp",   "si",   "di",
                               "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
static const char *kR8[16] = {"al",  "cl",  "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
                              "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};

static bool need(const x64_t *x, size_t k) {
    return x->i + k <= x->n;
}
static uint8_t get(x64_t *x) {
    return x->p[x->i++];
}
static uint8_t peek(const x64_t *x) {
    return x->p[x->i];
}

static const char *regn(uint8_t idx, int w) {
    idx &= 15u;
    if (w == 8)
        return kR8[idx];
    if (w == 16)
        return kR16[idx];
    if (w == 32)
        return kR32[idx];
    return kR64[idx];
}

static int width(const x64_t *x) {
    if (x->rex_w)
        return 64;
    if (x->op66)
        return 16;
    return 32;
}

static bool parse_prefixes(x64_t *x) {
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

static bool skip_modrm(x64_t *x) {
    uint8_t modrm, mod, rm;
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
            if (!need(x, 4))
                return false;
            x->i += 4;
        }
    }
    if (mod == 1) {
        if (!need(x, 1))
            return false;
        x->i += 1;
    } else if (mod == 2 || (mod == 0 && rm == 5)) {
        if (!need(x, 4))
            return false;
        x->i += 4;
    }
    return true;
}

static bool read_imm(x64_t *x, size_t nbytes, uint64_t *out) {
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

static void finish(re_disasm_insn_t *out, uint64_t addr, const uint8_t *buf, size_t end,
                   const char *text) {
    size_t len = end;
    size_t i = 0;
    if (len > 15)
        len = 15;
    out->addr = addr;
    out->size = (uint8_t)len;
    memcpy(out->bytes, buf, len);
    while (text[i] && i + 1u < sizeof(out->text)) {
        out->text[i] = text[i];
        i++;
    }
    out->text[i] = 0;
}

static bool dec_simple(x64_t *x, uint8_t op, char *t, size_t cap) {
    uint64_t imm = 0;
    if (op == 0x90) {
        snprintf(t, cap, "nop");
        return true;
    }
    if (op == 0xCC) {
        snprintf(t, cap, "int3");
        return true;
    }
    if (op == 0xC3) {
        snprintf(t, cap, "ret");
        return true;
    }
    if (op == 0xC2) {
        if (!read_imm(x, 2, &imm))
            return false;
        snprintf(t, cap, "ret 0x%llx", (unsigned long long)imm);
        return true;
    }
    if (op == 0xE8 || op == 0xE9) {
        if (!read_imm(x, 4, &imm))
            return false;
        snprintf(t, cap, "%s 0x%llx", op == 0xE8 ? "call" : "jmp", (unsigned long long)imm);
        return true;
    }
    if (op == 0xEB) {
        if (!read_imm(x, 1, &imm))
            return false;
        snprintf(t, cap, "jmp 0x%llx", (unsigned long long)imm);
        return true;
    }
    return false;
}

static bool dec_stack(x64_t *x, uint8_t op, char *t, size_t cap) {
    uint64_t imm = 0;
    if (op >= 0x50 && op <= 0x57) {
        snprintf(t, cap, "push %s", regn((uint8_t)((op - 0x50) | (x->rex_b << 3)), 64));
        return true;
    }
    if (op >= 0x58 && op <= 0x5F) {
        snprintf(t, cap, "pop %s", regn((uint8_t)((op - 0x58) | (x->rex_b << 3)), 64));
        return true;
    }
    if (op == 0x68) {
        if (!read_imm(x, 4, &imm))
            return false;
        snprintf(t, cap, "push 0x%llx", (unsigned long long)imm);
        return true;
    }
    if (op == 0x6A) {
        if (!read_imm(x, 1, &imm))
            return false;
        snprintf(t, cap, "push 0x%llx", (unsigned long long)imm);
        return true;
    }
    return false;
}

static bool dec_alu_imm(x64_t *x, uint8_t op, char *t, size_t cap) {
    static const char *names[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
    uint8_t modrm, reg;
    uint64_t imm = 0;
    size_t imm_n;
    int w = width(x);
    if (!need(x, 1))
        return false;
    modrm = peek(x);
    reg = (uint8_t)((modrm >> 3) & 7u);
    if (!skip_modrm(x))
        return false;
    imm_n = (op == 0x83 || op == 0x80) ? 1u : (w == 16 ? 2u : 4u);
    if (!read_imm(x, imm_n, &imm))
        return false;
    if ((modrm >> 6) == 3)
        snprintf(t, cap, "%s %s, 0x%llx", names[reg],
                 regn((uint8_t)((modrm & 7u) | (x->rex_b << 3)), w == 64 ? 64 : w),
                 (unsigned long long)imm);
    else
        snprintf(t, cap, "%s [mem], 0x%llx", names[reg], (unsigned long long)imm);
    return true;
}

static bool dec_mov(x64_t *x, uint8_t op, char *t, size_t cap) {
    uint8_t modrm, r, rm;
    int w = width(x);
    uint64_t imm = 0;
    if (op == 0x89 || op == 0x8B) {
        if (!need(x, 1))
            return false;
        modrm = peek(x);
        r = (uint8_t)(((modrm >> 3) & 7u) | (x->rex_r << 3));
        rm = (uint8_t)((modrm & 7u) | (x->rex_b << 3));
        if (!skip_modrm(x))
            return false;
        if ((modrm >> 6) == 3) {
            if (op == 0x89)
                snprintf(t, cap, "mov %s, %s", regn(rm, w == 64 ? 64 : w),
                         regn(r, w == 64 ? 64 : w));
            else
                snprintf(t, cap, "mov %s, %s", regn(r, w == 64 ? 64 : w),
                         regn(rm, w == 64 ? 64 : w));
        } else if (op == 0x89) {
            snprintf(t, cap, "mov [mem], %s", regn(r, w == 64 ? 64 : w));
        } else {
            snprintf(t, cap, "mov %s, [mem]", regn(r, w == 64 ? 64 : w));
        }
        return true;
    }
    if (op == 0x8D) {
        if (!need(x, 1))
            return false;
        modrm = peek(x);
        r = (uint8_t)(((modrm >> 3) & 7u) | (x->rex_r << 3));
        if (!skip_modrm(x))
            return false;
        snprintf(t, cap, "lea %s, [mem]", regn(r, 64));
        return true;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        size_t nimm = x->rex_w ? 8u : (x->op66 ? 2u : 4u);
        if (!read_imm(x, nimm, &imm))
            return false;
        snprintf(t, cap, "mov %s, 0x%llx",
                 regn((uint8_t)((op - 0xB8) | (x->rex_b << 3)), x->rex_w ? 64 : w),
                 (unsigned long long)imm);
        return true;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        if (!read_imm(x, 1, &imm))
            return false;
        snprintf(t, cap, "mov %s, 0x%llx", regn((uint8_t)((op - 0xB0) | (x->rex_b << 3)), 8),
                 (unsigned long long)imm);
        return true;
    }
    return false;
}

static const char *binop_name(uint8_t op) {
    if (op == 0x01 || op == 0x03)
        return "add";
    if (op == 0x29 || op == 0x2B)
        return "sub";
    if (op == 0x31 || op == 0x33)
        return "xor";
    if (op == 0x39 || op == 0x3B)
        return "cmp";
    if (op == 0x85)
        return "test";
    return NULL;
}

static bool dec_binop(x64_t *x, uint8_t op, char *t, size_t cap) {
    const char *name = binop_name(op);
    uint8_t modrm, r, rm;
    int w = width(x);
    if (!name)
        return false;
    if (!need(x, 1))
        return false;
    modrm = peek(x);
    r = (uint8_t)(((modrm >> 3) & 7u) | (x->rex_r << 3));
    rm = (uint8_t)((modrm & 7u) | (x->rex_b << 3));
    if (!skip_modrm(x))
        return false;
    if ((modrm >> 6) == 3) {
        if (op & 2u)
            snprintf(t, cap, "%s %s, %s", name, regn(r, w == 64 ? 64 : w),
                     regn(rm, w == 64 ? 64 : w));
        else
            snprintf(t, cap, "%s %s, %s", name, regn(rm, w == 64 ? 64 : w),
                     regn(r, w == 64 ? 64 : w));
    } else {
        snprintf(t, cap, "%s [mem], %s", name, regn(r, w == 64 ? 64 : w));
    }
    return true;
}

static bool dec_0f(x64_t *x, char *t, size_t cap) {
    uint8_t op2;
    uint64_t rel = 0;
    if (!need(x, 1))
        return false;
    op2 = get(x);
    if (op2 == 0x05) {
        snprintf(t, cap, "syscall");
        return true;
    }
    if (op2 == 0x1F || op2 == 0x0D) {
        if (!skip_modrm(x))
            return false;
        snprintf(t, cap, "nop");
        return true;
    }
    if (op2 >= 0x80 && op2 <= 0x8F) {
        if (!read_imm(x, 4, &rel))
            return false;
        snprintf(t, cap, "jcc 0x%llx", (unsigned long long)rel);
        return true;
    }
    if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
        if (!skip_modrm(x))
            return false;
        snprintf(t, cap, op2 >= 0xBE ? "movsx" : "movzx");
        return true;
    }
    (void)skip_modrm(x);
    snprintf(t, cap, "db 0x0f, 0x%02x", op2);
    return true;
}

static bool dec_misc(x64_t *x, uint8_t op, char *t, size_t cap) {
    uint64_t imm = 0;
    uint8_t modrm, reg;
    if (op >= 0x70 && op <= 0x7F) {
        if (!read_imm(x, 1, &imm))
            return false;
        snprintf(t, cap, "jcc 0x%llx", (unsigned long long)imm);
        return true;
    }
    if (op == 0xFF) {
        if (!need(x, 1))
            return false;
        modrm = peek(x);
        reg = (uint8_t)((modrm >> 3) & 7u);
        if (!skip_modrm(x))
            return false;
        if (reg == 2)
            snprintf(t, cap, "call [mem]");
        else if (reg == 4)
            snprintf(t, cap, "jmp [mem]");
        else if (reg == 6)
            snprintf(t, cap, "push [mem]");
        else
            snprintf(t, cap, "grp5");
        return true;
    }
    if (op == 0xC7) {
        if (!skip_modrm(x))
            return false;
        if (!read_imm(x, x->op66 ? 2u : 4u, &imm))
            return false;
        snprintf(t, cap, "mov [mem], 0x%llx", (unsigned long long)imm);
        return true;
    }
    return false;
}

static bool decode_body(x64_t *x, char *t, size_t cap) {
    uint8_t op;
    if (!need(x, 1))
        return false;
    op = get(x);
    if (dec_simple(x, op, t, cap) || dec_stack(x, op, t, cap))
        return true;
    if (op == 0x80 || op == 0x81 || op == 0x83)
        return dec_alu_imm(x, op, t, cap);
    if (dec_mov(x, op, t, cap) || dec_binop(x, op, t, cap))
        return true;
    if (op == 0x0F)
        return dec_0f(x, t, cap);
    if (dec_misc(x, op, t, cap))
        return true;
    if ((op >= 0x88 && op <= 0x8B) || op == 0x8D || op == 0xC6 || op == 0xF6 || op == 0xF7 ||
        op == 0xFE) {
        if (!skip_modrm(x))
            return false;
    }
    snprintf(t, cap, "db 0x%02x", op);
    return true;
}

size_t re_disasm_x64_one(uint64_t addr, const uint8_t *buf, size_t n, re_disasm_insn_t *out) {
    x64_t x;
    char text[RE_DISASM_TEXT_MAX];
    if (!buf || !out || n == 0)
        return 0;
    memset(out, 0, sizeof(*out));
    memset(&x, 0, sizeof(x));
    x.p = buf;
    x.n = n > 15 ? 15 : n;
    if (!parse_prefixes(&x) || !decode_body(&x, text, sizeof(text))) {
        finish(out, addr, buf, 1, "db");
        snprintf(out->text, sizeof(out->text), "db 0x%02x", buf[0]);
        return 1;
    }
    finish(out, addr, buf, x.i, text);
    return out->size ? out->size : 1;
}

re_err_code_t re_disasm_x64_many(uint64_t addr, const uint8_t *buf, size_t n, size_t max_n,
                                 re_disasm_insn_t *out, size_t *n_out) {
    size_t off = 0;
    size_t count = 0;
    if (n_out)
        *n_out = 0;
    if (!buf || !out || max_n == 0)
        return RE_E_USAGE;
    while (off < n && count < max_n) {
        size_t len = re_disasm_x64_one(addr + off, buf + off, n - off, &out[count]);
        if (len == 0)
            break;
        off += len;
        count++;
    }
    if (n_out)
        *n_out = count;
    return RE_OK;
}
