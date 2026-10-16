// re_patch_file.c - PE/ELF VA-to-file-offset and patch write-back.
// Module: feature/patch (C11).
// Owns: section/PT_LOAD mapping and copying patched images to disk.
// Depends: re_patch.h, re_dbg, re_buf, re_str.
#include "features/patch/re_patch.h"

#include "features/debug/re_dbg.h"
#include "utils/mem/re_buf.h"
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

static uint32_t pe_rva_to_off(re_span_t file, uint32_t rva, uint32_t sect_off, uint16_t nsect) {
    uint32_t i;
    for (i = 0; i < nsect; i++) {
        uint32_t off = sect_off + i * 40u;
        uint32_t vsize = 0, vaddr = 0, rawsz = 0, rawptr = 0;
        if (!re_rd32(file, off + 8, &vsize) || !re_rd32(file, off + 12, &vaddr) ||
            !re_rd32(file, off + 16, &rawsz) || !re_rd32(file, off + 20, &rawptr))
            return 0;
        if (rva >= vaddr && rva < vaddr + (vsize > rawsz ? vsize : rawsz))
            return rawptr + (rva - vaddr);
    }
    return 0;
}

static re_err_code_t pe_va_to_off(re_span_t file, uint64_t image_base, uint64_t va,
                                  uint64_t *file_off, uint64_t *rva_out) {
    uint32_t e_lfanew = 0, rva32, off;
    uint16_t nsect = 0, opt_sz = 0;
    uint32_t sect_off;
    if (file.n < 0x40 || file.p[0] != 'M' || file.p[1] != 'Z')
        return RE_E_NOTBIN;
    if (va < image_base)
        return RE_E_RANGE;
    *rva_out = va - image_base;
    if (*rva_out > 0xffffffffu)
        return RE_E_RANGE;
    rva32 = (uint32_t)*rva_out;
    if (!re_rd32(file, 0x3c, &e_lfanew) || e_lfanew + 0x18 >= file.n)
        return RE_E_MALFORMED;
    if (file.p[e_lfanew] != 'P' || file.p[e_lfanew + 1] != 'E')
        return RE_E_MALFORMED;
    if (!re_rd16(file, e_lfanew + 6, &nsect) || !re_rd16(file, e_lfanew + 20, &opt_sz))
        return RE_E_MALFORMED;
    sect_off = e_lfanew + 24u + opt_sz;
    off = pe_rva_to_off(file, rva32, sect_off, nsect);
    if (!off)
        return RE_E_NOTFOUND;
    *file_off = off;
    return RE_OK;
}

static re_err_code_t elf_read_phdr(re_span_t file, uint8_t cls, uint64_t off, uint32_t *type,
                                   uint64_t *p_offset, uint64_t *p_vaddr, uint64_t *p_filesz) {
    if (cls == 2) {
        return (re_rd32(file, off, type) && re_rd64(file, off + 8, p_offset) &&
                re_rd64(file, off + 16, p_vaddr) && re_rd64(file, off + 32, p_filesz))
                   ? RE_OK
                   : RE_E_MALFORMED;
    }
    {
        uint32_t o32 = 0, v32 = 0, f32 = 0;
        if (!re_rd32(file, off, type) || !re_rd32(file, off + 4, &o32) ||
            !re_rd32(file, off + 8, &v32) || !re_rd32(file, off + 16, &f32))
            return RE_E_MALFORMED;
        *p_offset = o32;
        *p_vaddr = v32;
        *p_filesz = f32;
        return RE_OK;
    }
}

static re_err_code_t elf_min_load_vaddr(re_span_t file, uint8_t cls, uint64_t phoff,
                                        uint16_t phentsz, uint16_t phnum, uint64_t *min_out) {
    uint16_t i;
    uint64_t min_vaddr = UINT64_MAX;
    for (i = 0; i < phnum; i++) {
        uint32_t type = 0;
        uint64_t p_offset = 0, p_vaddr = 0, p_filesz = 0;
        re_err_code_t c = elf_read_phdr(file, cls, phoff + (uint64_t)i * phentsz, &type, &p_offset,
                                        &p_vaddr, &p_filesz);
        if (c != RE_OK)
            return c;
        if (type == 1 && p_vaddr < min_vaddr)
            min_vaddr = p_vaddr;
        (void)p_offset;
        (void)p_filesz;
    }
    if (min_vaddr == UINT64_MAX)
        return RE_E_NOTFOUND;
    *min_out = min_vaddr;
    return RE_OK;
}

static re_err_code_t elf_va_to_off(re_span_t file, uint64_t image_base, uint64_t va,
                                   uint64_t *file_off, uint64_t *rva_out) {
    uint8_t cls;
    uint64_t phoff = 0, min_vaddr = 0;
    uint16_t phentsz = 0, phnum = 0, i;
    re_err_code_t c;
    if (file.n < 64 || file.p[0] != 0x7f || file.p[1] != 'E' || file.p[2] != 'L' ||
        file.p[3] != 'F')
        return RE_E_NOTBIN;
    if (va < image_base)
        return RE_E_RANGE;
    *rva_out = va - image_base;
    cls = file.p[4];
    if (cls == 2) {
        if (!re_rd64(file, 32, &phoff) || !re_rd16(file, 54, &phentsz) ||
            !re_rd16(file, 56, &phnum))
            return RE_E_MALFORMED;
    } else if (cls == 1) {
        uint32_t ph32 = 0;
        if (!re_rd32(file, 28, &ph32) || !re_rd16(file, 42, &phentsz) || !re_rd16(file, 44, &phnum))
            return RE_E_MALFORMED;
        phoff = ph32;
    } else {
        return RE_E_UNSUPPORTED;
    }
    c = elf_min_load_vaddr(file, cls, phoff, phentsz, phnum, &min_vaddr);
    if (c != RE_OK)
        return c;
    for (i = 0; i < phnum; i++) {
        uint32_t type = 0;
        uint64_t p_offset = 0, p_vaddr = 0, p_filesz = 0, seg_base;
        c = elf_read_phdr(file, cls, phoff + (uint64_t)i * phentsz, &type, &p_offset, &p_vaddr,
                          &p_filesz);
        if (c != RE_OK)
            return c;
        if (type != 1)
            continue;
        seg_base = image_base + (p_vaddr - min_vaddr);
        if (va < seg_base || va >= seg_base + p_filesz)
            continue;
        *file_off = p_offset + (va - seg_base);
        return RE_OK;
    }
    return RE_E_NOTFOUND;
}

static re_err_code_t span_va_to_off(re_span_t whole, uint64_t image_base, uint64_t va,
                                    uint64_t *file_off, uint64_t *rva_out) {
    if (whole.n >= 4 && whole.p[0] == 0x7f && whole.p[1] == 'E')
        return elf_va_to_off(whole, image_base, va, file_off, rva_out);
    return pe_va_to_off(whole, image_base, va, file_off, rva_out);
}

re_err_code_t re_patch_va_to_file_off(const char *path, uint64_t image_base, uint64_t va,
                                      uint64_t *file_off_out, uint64_t *rva_out) {
    re_arena_t a;
    re_file_t f;
    re_err_code_t c;
    if (!path || !file_off_out || !rva_out)
        return RE_E_USAGE;
    re_arena_init(&a, 64u * 1024u);
    c = re_file_open(path, &a, &f);
    if (c != RE_OK) {
        re_arena_free(&a);
        return c;
    }
    c = span_va_to_off(f.whole, image_base, va, file_off_out, rva_out);
    re_file_close(&f);
    re_arena_free(&a);
    return c;
}

static void default_out_path(const char *mod, char *out, size_t cap) {
    size_t n = 0;
    while (mod[n])
        n++;
    if (n + 9 >= cap) {
        out[0] = 0;
        return;
    }
    memcpy(out, mod, n);
    memcpy(out + n, ".patched", 9);
}

static re_err_code_t patch_file_slots(FILE *out, size_t file_n, const re_patch_list_t *pl,
                                      const char *mod_path, uint64_t module_base, re_span_t whole) {
    size_t i;
    for (i = 0; i < pl->n; i++) {
        uint64_t foff = 0, rva = 0;
        const re_patch_entry_t *e = &pl->items[i];
        re_err_code_t c;
        if (!e->used || !e->module[0])
            continue;
        if (!re_str_eq_cstr(re_str(e->module), mod_path))
            continue;
        c = span_va_to_off(whole, module_base ? module_base : e->module_base, e->addr, &foff, &rva);
        if (c != RE_OK)
            return c;
        if (foff + e->size > file_n)
            return RE_E_RANGE;
        if (fseek(out, (long)foff, SEEK_SET) != 0)
            return RE_E_IO;
        if (fwrite(e->bytes, 1, e->size, out) != e->size)
            return RE_E_IO;
    }
    return RE_OK;
}

static re_err_code_t write_one_module(const char *mod_path, const char *out_path,
                                      const re_patch_list_t *pl, uint64_t module_base) {
    re_arena_t a;
    re_file_t f;
    re_err_code_t c;
    FILE *out;
    re_arena_init(&a, 64u * 1024u);
    c = re_file_open(mod_path, &a, &f);
    if (c != RE_OK) {
        re_arena_free(&a);
        return c;
    }
    out = fopen(out_path, "w+b");
    if (!out) {
        re_file_close(&f);
        re_arena_free(&a);
        return RE_E_IO;
    }
    if (fwrite(f.whole.p, 1, f.whole.n, out) != f.whole.n) {
        fclose(out);
        re_file_close(&f);
        re_arena_free(&a);
        return RE_E_IO;
    }
    fflush(out);
    c = patch_file_slots(out, f.whole.n, pl, mod_path, module_base, f.whole);
    fclose(out);
    re_file_close(&f);
    re_arena_free(&a);
    return c;
}

static size_t collect_modules(const re_patch_list_t *pl, char seen[][RE_PATCH_PATH_MAX],
                              uint64_t *bases, size_t cap) {
    size_t n_seen = 0, i, j;
    for (i = 0; i < pl->n; i++) {
        const re_patch_entry_t *e = &pl->items[i];
        bool dup = false;
        if (!e->used || !e->module[0])
            continue;
        for (j = 0; j < n_seen; j++) {
            if (re_str_eq_cstr(re_str(seen[j]), e->module)) {
                dup = true;
                break;
            }
        }
        if (dup || n_seen >= cap)
            continue;
        copy_cstr(seen[n_seen], RE_PATCH_PATH_MAX, e->module);
        bases[n_seen] = e->module_base;
        n_seen++;
    }
    return n_seen;
}

re_err_code_t re_dbg_session_patch_writeback(re_dbg_session_t *s, const char *out_path,
                                             re_arena_t *scratch, size_t *n_files_out) {
    char seen[RE_PATCH_MAX][RE_PATCH_PATH_MAX];
    uint64_t bases[RE_PATCH_MAX];
    size_t n_seen, i, written = 0;
    (void)scratch;
    if (!s)
        return RE_E_USAGE;
    if (n_files_out)
        *n_files_out = 0;
    n_seen = collect_modules(&s->patches, seen, bases, RE_PATCH_MAX);
    for (i = 0; i < n_seen; i++) {
        char dest[RE_PATCH_PATH_MAX];
        re_err_code_t c;
        if (out_path && out_path[0] && n_seen == 1)
            copy_cstr(dest, sizeof(dest), out_path);
        else
            default_out_path(seen[i], dest, sizeof(dest));
        if (!dest[0])
            return RE_E_USAGE;
        c = write_one_module(seen[i], dest, &s->patches, bases[i]);
        if (c != RE_OK)
            return c;
        written++;
    }
    if (n_files_out)
        *n_files_out = written;
    return written ? RE_OK : RE_E_NOTFOUND;
}
