// test_patch_p2.c - Phase 2 Pack H: assemble, patch list, PE/ELF write-back.
// Module: test (C11).
// Owns: assembler roundtrips, list upsert, and file-offset mapping smoke.
// Depends: re_asm, re_patch, re_test.
#include "features/debug/re_dbg.h"
#include "features/patch/re_asm.h"
#include "features/patch/re_patch.h"
#include "re_test.h"

#include <stdio.h>
#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_asm_basic(void) {
    uint8_t out[16];
    size_t n = 0;
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "nop", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(n, 1);
    RE_CHECK_EQ_U(out[0], 0x90);
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "int3", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(out[0], 0xCC);
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "ret", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(out[0], 0xC3);
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "xor rax, rax", out, sizeof(out), &n), RE_OK);
    RE_CHECK(n >= 2);
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "mov rax, rbx", out, sizeof(out), &n), RE_OK);
    RE_CHECK(n >= 2);
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "db 90 cc", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(n, 2);
    RE_CHECK_EQ_U(out[0], 0x90);
    RE_CHECK_EQ_U(out[1], 0xCC);
}

static void test_asm_branch(void) {
    uint8_t out[16];
    size_t n = 0;
    /* jmp from 0x1000 to 0x1005 => rel8 = 3 (after 2-byte insn) wait: target-addr-2 */
    RE_CHECK_EQ_U(re_asm_x64(0x1000, "jmp 0x1002", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(n, 2);
    RE_CHECK_EQ_U(out[0], 0xEB);
    RE_CHECK_EQ_U(out[1], 0x00);
    RE_CHECK_EQ_U(re_asm_x64(0x2000, "call 0x2010", out, sizeof(out), &n), RE_OK);
    RE_CHECK_EQ_U(n, 5);
    RE_CHECK_EQ_U(out[0], 0xE8);
}

static void test_patch_list(void) {
    re_patch_list_t pl;
    uint8_t orig[1] = {0x90};
    uint8_t bytes[1] = {0xCC};
    re_patch_list_init(&pl);
    RE_CHECK_EQ_U(
        re_patch_list_add(&pl, 0x401000, orig, bytes, 1, "int3", "/tmp/x", 0x400000, 0x1000),
        RE_OK);
    RE_CHECK_EQ_U(pl.n, 1);
    bytes[0] = 0x90;
    RE_CHECK_EQ_U(
        re_patch_list_add(&pl, 0x401000, orig, bytes, 1, "nop", "/tmp/x", 0x400000, 0x1000), RE_OK);
    RE_CHECK_EQ_U(pl.n, 1);
    RE_CHECK_EQ_U(pl.items[0].bytes[0], 0x90);
}

static void write_bytes(const char *path, const uint8_t *p, size_t n) {
    FILE *f = fopen(path, "wb");
    RE_CHECK(f != NULL);
    RE_CHECK(fwrite(p, 1, n, f) == n);
    fclose(f);
}

static void test_elf_va_to_off(void) {
    /* Minimal ELF64: ehdr + one PT_LOAD at file off 0x100 covering vaddr 0. */
    uint8_t elf[0x200];
    uint64_t foff = 0, rva = 0;
    const char *path = "test_patch_elf.bin";
    memset(elf, 0, sizeof(elf));
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;  /* ELFCLASS64 */
    elf[5] = 1;  /* LSB */
    elf[16] = 3; /* ET_DYN */
    /* e_phoff = 64 */
    elf[32] = 64;
    /* e_phentsize = 56, e_phnum = 1 */
    elf[54] = 56;
    elf[56] = 1;
    /* Phdr at 64: PT_LOAD, p_offset=0x100, p_vaddr=0, p_filesz=0x100 */
    elf[64] = 1; /* p_type */
    elf[64 + 8] = 0x00;
    elf[64 + 9] = 0x01; /* p_offset = 0x100 */
    elf[64 + 32] = 0x00;
    elf[64 + 33] = 0x01; /* p_filesz = 0x100 */
    write_bytes(path, elf, sizeof(elf));
    RE_CHECK_EQ_U(re_patch_va_to_file_off(path, 0x100000, 0x100010, &foff, &rva), RE_OK);
    RE_CHECK_EQ_U(rva, 0x10ull);
    RE_CHECK_EQ_U(foff, 0x110ull);
}

static void test_pe_va_to_off(void) {
    uint8_t pe[0x400];
    uint64_t foff = 0, rva = 0;
    uint32_t sect_off;
    const char *path = "test_patch_pe.bin";
    memset(pe, 0, sizeof(pe));
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3c] = 0x80; /* e_lfanew */
    pe[0x80] = 'P';
    pe[0x81] = 'E';
    pe[0x80 + 6] = 1;     /* NumberOfSections */
    pe[0x80 + 20] = 0xF0; /* SizeOfOptionalHeader = 0xF0 (PE32+) */
    pe[0x80 + 24] = 0x0b;
    pe[0x80 + 25] = 0x02; /* PE32+ magic */
    sect_off = 0x80 + 24 + 0xF0;
    /* VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData */
    pe[sect_off + 8] = 0x00;
    pe[sect_off + 9] = 0x10; /* vsize 0x1000 */
    pe[sect_off + 12] = 0x00;
    pe[sect_off + 13] = 0x10; /* vaddr 0x1000 */
    pe[sect_off + 16] = 0x00;
    pe[sect_off + 17] = 0x02; /* raw size 0x200 */
    pe[sect_off + 20] = 0x00;
    pe[sect_off + 21] = 0x02; /* raw ptr 0x200 */
    write_bytes(path, pe, sizeof(pe));
    RE_CHECK_EQ_U(re_patch_va_to_file_off(path, 0x400000, 0x401010, &foff, &rva), RE_OK);
    RE_CHECK_EQ_U(rva, 0x1010ull);
    RE_CHECK_EQ_U(foff, 0x210ull);
}

static void test_writeback_elf(void) {
    re_patch_list_t pl;
    uint8_t orig[1] = {0x00};
    uint8_t bytes[1] = {0xCC};
    uint8_t elf[0x200];
    const char *src = "test_patch_wb_src.bin";
    const char *dst = "test_patch_wb_out.bin";
    FILE *f;
    uint8_t got = 0;
    re_dbg_session_t s;
    memset(elf, 0, sizeof(elf));
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;
    elf[5] = 1;
    elf[16] = 3;
    elf[32] = 64;
    elf[54] = 56;
    elf[56] = 1;
    elf[64] = 1;
    elf[64 + 9] = 0x01;
    elf[64 + 33] = 0x01;
    elf[0x110] = 0x90;
    write_bytes(src, elf, sizeof(elf));
    re_patch_list_init(&pl);
    RE_CHECK_EQ_U(re_patch_list_add(&pl, 0x100010, orig, bytes, 1, "int3", src, 0x100000, 0x10),
                  RE_OK);
    memset(&s, 0, sizeof(s));
    s.patches = pl;
    RE_CHECK_EQ_U(re_dbg_session_patch_writeback(&s, dst, NULL, NULL), RE_OK);
    f = fopen(dst, "rb");
    RE_CHECK(f != NULL);
    RE_CHECK(fseek(f, 0x110, SEEK_SET) == 0);
    RE_CHECK(fread(&got, 1, 1, f) == 1);
    fclose(f);
    RE_CHECK_EQ_U(got, 0xCC);
}

int main(void) {
    test_asm_basic();
    test_asm_branch();
    test_patch_list();
    test_elf_va_to_off();
    test_pe_va_to_off();
    test_writeback_elf();
    return re_test_report("patch_p2");
}
