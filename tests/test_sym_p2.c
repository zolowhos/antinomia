// test_sym_p2.c - Phase 2 pack F: symbol providers, DWARF lines, rich user DB.
// Module: test (C11).
// Owns: PDB/DWARF provider smoke, source lookup, bookmark/patch/func roundtrip.
// Depends: re_sym_prov, re_dbg_db, re_test. DWARF path needs dwarf_src -g binary.
#include "features/persist/re_dbg_db.h"
#include "features/sym/re_sym_prov.h"
#include "utils/mem/re_arena.h"
#include "re_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

#if defined(__linux__)
static const char *g_dwarf_bin;
#endif

static void test_pdb_provider_stub(void) {
    re_arena_t a;
    re_sym_provider_t *p;
    re_err_code_t c;
    re_arena_init(&a, 64 * 1024);
    p = re_sym_pdb_create(&a);
    RE_CHECK(p != NULL);
    RE_CHECK(p->vt != NULL);
    RE_CHECK(strcmp(p->vt->name, "pdb_dbghelp") == 0);
    c = p->vt->open_module(p, &a, "/nonexistent", 0x1000);
#if defined(_WIN32)
    /* Missing image/PDB should fail IO or unsupported without a real module. */
    RE_CHECK(c == RE_E_IO || c == RE_E_UNSUPPORTED || c == RE_E_NOTFOUND);
#else
    RE_CHECK_EQ_U(c, RE_E_UNSUPPORTED);
#endif
    re_arena_free(&a);
}

static void test_export_provider(void) {
    re_arena_t a;
    re_sym_provider_t *p;
    re_arena_init(&a, 64 * 1024);
    p = re_sym_export_create(&a);
    RE_CHECK(p != NULL);
    RE_CHECK(strcmp(p->vt->name, "export") == 0);
    RE_CHECK_EQ_U(p->vt->addr_to_line(p, 0, NULL), RE_E_UNSUPPORTED);
    re_arena_free(&a);
}

static void test_rich_user_db(void) {
    re_dbg_db_t db;
    re_arena_t scratch;
    re_dbg_db_func_t fn;
#if defined(_WIN32)
    char path[512];
    {
        const char *tmp = getenv("TEMP");
        if (!tmp || !tmp[0])
            tmp = getenv("TMP");
        if (!tmp || !tmp[0])
            tmp = ".";
        (void)snprintf(path, sizeof(path), "%s\\antinomia_packf_mod.dll", tmp);
    }
#else
    char path[] = "/tmp/antinomia_packf_mod.so";
#endif
    char dbpath[512];
    re_arena_init(&scratch, 256 * 1024);
    re_dbg_db_init(&db);
    RE_CHECK_EQ_U(re_dbg_db_path_for(path, dbpath, sizeof(dbpath)), RE_OK);
    {
        size_t i = 0;
        while (path[i] && i + 1u < sizeof(db.module_path)) {
            db.module_path[i] = path[i];
            i++;
        }
        db.module_path[i] = 0;
        i = 0;
        while (dbpath[i] && i + 1u < sizeof(db.db_path)) {
            db.db_path[i] = dbpath[i];
            i++;
        }
        db.db_path[i] = 0;
    }
    RE_CHECK_EQ_U(re_dbg_db_add_bookmark(&db, 0x1000, "entry"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_bookmark(&db, 0x2000, "loop"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_patch(&db, 0x3000, 2, "9090", "cc90"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_func(&db, 0x1000, 0x1200, "main"), RE_OK);
    RE_CHECK_EQ_U(re_dbg_db_add_func(&db, 0x2000, 0x2100, "tick"), RE_OK);
    RE_CHECK_EQ_U(db.n_bookmarks, 2);
    RE_CHECK_EQ_U(db.n_patches, 1);
    RE_CHECK_EQ_U(db.n_funcs, 2);
    RE_CHECK_EQ_U(re_dbg_db_find_func(&db, 0x1050, &fn), RE_OK);
    RE_CHECK(strcmp(fn.name, "main") == 0);
    RE_CHECK_EQ_U(re_dbg_db_find_func(&db, 0x3000, &fn), RE_E_NOTFOUND);
    RE_CHECK_EQ_U(re_dbg_db_save(&db, &scratch), RE_OK);
    re_dbg_db_clear(&db);
    RE_CHECK_EQ_U(re_dbg_db_load(&db, &scratch, path), RE_OK);
    RE_CHECK_EQ_U(db.n_bookmarks, 2);
    RE_CHECK_EQ_U(db.n_patches, 1);
    RE_CHECK_EQ_U(db.n_funcs, 2);
    RE_CHECK(strcmp(db.bookmarks[0].name, "entry") == 0);
    RE_CHECK(strcmp(db.patches[0].patch_hex, "cc90") == 0);
    RE_CHECK(strcmp(db.funcs[1].name, "tick") == 0);
    remove(dbpath);
    re_arena_free(&scratch);
}

#if defined(__linux__)
static void test_dwarf_line_from_fixture(void) {
    re_arena_t a;
    re_sym_provider_t *d;
    re_sym_line_t line;
    re_err_code_t c;
    uint64_t addr = 0;
    re_dbg_symbol_t info;
    if (!g_dwarf_bin || !g_dwarf_bin[0]) {
        fprintf(stderr, "skip dwarf: no fixture\n");
        return;
    }
    re_arena_init(&a, 1024 * 1024);
    d = re_sym_dwarf_create(&a);
    RE_CHECK(d != NULL);
    RE_CHECK(strcmp(d->vt->name, "dwarf_line") == 0);
    c = d->vt->open_module(d, &a, g_dwarf_bin, 0);
    RE_CHECK(c == RE_OK || c == RE_E_NOTFOUND || c == RE_E_UNSUPPORTED || c == RE_E_MALFORMED);
    if (c != RE_OK) {
        re_arena_free(&a);
        return;
    }
    /* Prefer a code address from ELF symtab if available. */
    if (re_dbg_sym_resolve_file(&a, g_dwarf_bin, 0, "antinomia_dwarf_marker", &addr) == RE_OK &&
        addr != 0) {
        c = d->vt->addr_to_line(d, addr, &line);
        RE_CHECK_EQ_U(c, RE_OK);
        RE_CHECK(line.line > 0);
        RE_CHECK(line.file[0] != 0);
        fprintf(stderr, "dwarf: %s:%u @ 0x%llx\n", line.file, line.line,
                (unsigned long long)line.addr);
    } else {
        /* Still exercise provider close path. */
        (void)info;
        RE_CHECK(d->vt->close_module(d) == RE_OK);
    }
    (void)d->vt->close_module(d);
    re_arena_free(&a);
}
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
    g_dwarf_bin = getenv("RE_TEST_DWARF");
    if ((!g_dwarf_bin || !g_dwarf_bin[0]) && argc > 1)
        g_dwarf_bin = argv[1];
#else
    (void)argc;
    (void)argv;
#endif
    test_pdb_provider_stub();
    test_export_provider();
    test_rich_user_db();
#if defined(__linux__)
    test_dwarf_line_from_fixture();
#endif
    return re_test_report("sym_p2");
}
