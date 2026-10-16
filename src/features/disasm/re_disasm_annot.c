// re_disasm_annot.c - module and string comments on disasm immediates.
// Module: feature (C11).
// Owns: printable-string probe and module+rva recognition for insn comments.
// Depends: re_disasm_annot.h, re_path, re_str. Reads target memory via session.
#include "features/disasm/re_disasm_annot.h"

#include "utils/sys/re_path.h"
#include "utils/text/re_str.h"

#include <stdio.h>
#include <string.h>

static void copy_cstr(char *dst, size_t cap, const char *src) {
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

static bool is_print(uint8_t c) {
    return c >= 0x20 && c < 0x7f;
}

static bool look_string(re_dbg_session_t *s, uint64_t addr, char *out, size_t cap) {
    uint8_t buf[40];
    size_t i = 0;
    size_t n = 0;
    if (!s || !out || cap < 4)
        return false;
    if (addr < 0x1000ull)
        return false;
    if (re_dbg_session_read_mem(s, addr, buf, sizeof(buf)) != RE_OK)
        return false;
    while (n < sizeof(buf) && is_print(buf[n]))
        n++;
    if (n < 3)
        return false;
    if (n < sizeof(buf) && buf[n] != 0 && !is_print(buf[n]))
        return false;
    out[i++] = '"';
    while (i + 2u < cap && i - 1u < n && i - 1u < 24u) {
        out[i] = (char)buf[i - 1u];
        i++;
    }
    if (n > 24u && i + 4u < cap) {
        out[i++] = '.';
        out[i++] = '.';
        out[i++] = '.';
    }
    out[i++] = '"';
    out[i] = 0;
    return true;
}

static bool look_module(re_dbg_session_t *s, uint64_t addr, char *out, size_t cap) {
    re_dbg_module_t mods[64];
    size_t n = 0, i;
    if (!s || !out || cap < 8)
        return false;
    if (re_dbg_session_enum_modules(s, mods, 64, &n) != RE_OK || n == 0)
        return false;
    for (i = 0; i < n; i++) {
        uint64_t base = mods[i].base;
        uint64_t end = base + mods[i].size;
        const char *base_name;
        if (addr < base || (mods[i].size && addr >= end))
            continue;
        base_name = re_path_basename_ptr(mods[i].path);
        if (!base_name || !base_name[0])
            base_name = mods[i].path;
        (void)snprintf(out, cap, "%s+0x%llx", base_name, (unsigned long long)(addr - base));
        return true;
    }
    return false;
}

static bool look_db_comment(re_dbg_session_t *s, uint64_t addr, char *out, size_t cap) {
    size_t i;
    re_dbg_module_t mods[16];
    size_t n = 0;
    if (!s || s->db.n_comments == 0)
        return false;
    if (re_dbg_session_enum_modules(s, mods, 16, &n) != RE_OK)
        n = 0;
    for (i = 0; i < s->db.n_comments; i++) {
        uint64_t abs = s->db.comments[i].rva;
        size_t m;
        if (n > 0)
            abs = mods[0].base + s->db.comments[i].rva;
        if (abs != addr)
            continue;
        copy_cstr(out, cap, s->db.comments[i].text);
        for (m = 0; out[m]; m++) {
            if (out[m] == '\n' || out[m] == '\r')
                out[m] = ' ';
        }
        return out[0] != 0;
    }
    return false;
}

void re_disasm_annot_one(re_dbg_session_t *s, re_disasm_insn_t *insn) {
    char tmp[RE_DISASM_COMMENT_MAX];
    uint64_t cand = 0;
    if (!insn)
        return;
    insn->comment[0] = 0;
    re_disasm_enrich(insn);
    if (look_db_comment(s, insn->addr, tmp, sizeof(tmp))) {
        copy_cstr(insn->comment, sizeof(insn->comment), tmp);
        return;
    }
    if (insn->flags & (RE_DISASM_F_CALL | RE_DISASM_F_JMP | RE_DISASM_F_JCC))
        cand = insn->target;
    else if (insn->flags & (RE_DISASM_F_IMM | RE_DISASM_F_RIPREL))
        cand = insn->mem;
    if (!cand)
        return;
    if (look_string(s, cand, tmp, sizeof(tmp))) {
        copy_cstr(insn->comment, sizeof(insn->comment), tmp);
        return;
    }
    if (look_module(s, cand, tmp, sizeof(tmp)))
        copy_cstr(insn->comment, sizeof(insn->comment), tmp);
}

void re_disasm_annot_many(re_dbg_session_t *s, re_disasm_insn_t *lines, size_t n) {
    size_t i;
    if (!lines)
        return;
    for (i = 0; i < n; i++)
        re_disasm_annot_one(s, &lines[i]);
}
