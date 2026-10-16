// re_patch.c - live patch list and assemble-into-memory apply path.
// Module: feature/patch (C11).
// Owns: upsert/clear of session patches; assemble + write_mem + DB record.
// Depends: re_patch.h, re_dbg, re_hex, re_str.
#include "features/patch/re_patch.h"

#include "features/debug/re_dbg.h"
#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"

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

void re_patch_list_init(re_patch_list_t *pl) {
    if (pl)
        memset(pl, 0, sizeof(*pl));
}

void re_patch_list_clear(re_patch_list_t *pl) {
    re_patch_list_init(pl);
}

re_err_code_t re_patch_list_add(re_patch_list_t *pl, uint64_t addr, const uint8_t *orig,
                                const uint8_t *bytes, uint32_t size, const char *text,
                                const char *module, uint64_t module_base, uint64_t rva) {
    size_t i;
    re_patch_entry_t *e;
    if (!pl || !orig || !bytes || size == 0 || size > RE_ASM_MAX_BYTES)
        return RE_E_USAGE;
    for (i = 0; i < pl->n; i++) {
        if (pl->items[i].addr == addr) {
            e = &pl->items[i];
            e->size = size;
            memcpy(e->orig, orig, size);
            memcpy(e->bytes, bytes, size);
            copy_cstr(e->text, sizeof(e->text), text ? text : "");
            copy_cstr(e->module, sizeof(e->module), module ? module : "");
            e->module_base = module_base;
            e->rva = rva;
            e->used = true;
            return RE_OK;
        }
    }
    if (pl->n >= RE_PATCH_MAX)
        return RE_E_RANGE;
    e = &pl->items[pl->n++];
    memset(e, 0, sizeof(*e));
    e->addr = addr;
    e->size = size;
    memcpy(e->orig, orig, size);
    memcpy(e->bytes, bytes, size);
    copy_cstr(e->text, sizeof(e->text), text ? text : "");
    copy_cstr(e->module, sizeof(e->module), module ? module : "");
    e->module_base = module_base;
    e->rva = rva;
    e->used = true;
    return RE_OK;
}

static void find_module(re_dbg_session_t *s, uint64_t addr, char *path, size_t path_cap,
                        uint64_t *base_out, uint64_t *rva_out) {
    re_dbg_module_t mods[64];
    size_t n = 0, i, best = (size_t)-1;
    uint64_t best_base = 0;
    path[0] = 0;
    *base_out = 0;
    *rva_out = 0;
    if (re_dbg_session_enum_modules(s, mods, 64, &n) != RE_OK)
        return;
    for (i = 0; i < n; i++) {
        if (addr < mods[i].base)
            continue;
        if (mods[i].size && addr >= mods[i].base + mods[i].size)
            continue;
        if (best == (size_t)-1 || mods[i].base > best_base) {
            best = i;
            best_base = mods[i].base;
        }
    }
    if (best == (size_t)-1)
        return;
    copy_cstr(path, path_cap, mods[best].path);
    *base_out = mods[best].base;
    *rva_out = addr - mods[best].base;
}

static re_err_code_t record_db(re_dbg_session_t *s, uint64_t rva, const uint8_t *orig,
                               const uint8_t *bytes, uint32_t size) {
    char oh[RE_PATCH_HEX_MAX];
    char ph[RE_PATCH_HEX_MAX];
    if (size * 2u + 1u > sizeof(oh))
        return RE_E_RANGE;
    re_hex_encode(oh, orig, size);
    re_hex_encode(ph, bytes, size);
    return re_dbg_db_add_patch(&s->db, rva, size, oh, ph);
}

re_err_code_t re_dbg_session_assemble(re_dbg_session_t *s, uint64_t addr, const char *text,
                                      size_t *n_out) {
    uint8_t bytes[RE_ASM_MAX_BYTES];
    uint8_t orig[RE_ASM_MAX_BYTES];
    size_t n = 0;
    re_err_code_t c;
    char mod[RE_PATCH_PATH_MAX];
    uint64_t base = 0, rva = 0;
    if (!s || !text)
        return RE_E_USAGE;
    if (n_out)
        *n_out = 0;
    c = re_asm_x64(addr, text, bytes, sizeof(bytes), &n);
    if (c != RE_OK)
        return c;
    c = re_dbg_session_read_mem(s, addr, orig, n);
    if (c != RE_OK)
        return c;
    c = re_dbg_session_write_mem(s, addr, bytes, n);
    if (c != RE_OK)
        return c;
    find_module(s, addr, mod, sizeof(mod), &base, &rva);
    c = re_patch_list_add(&s->patches, addr, orig, bytes, (uint32_t)n, text, mod, base, rva);
    if (c != RE_OK)
        return c;
    if (mod[0])
        (void)record_db(s, rva, orig, bytes, (uint32_t)n);
    if (n_out)
        *n_out = n;
    return RE_OK;
}
