// test_disasm_p2.c - Phase 2 Pack E: enrich, CFG, find parse, annot helpers.
// Module: test (C11).
// Owns: unit coverage for disasm meta, basic-block CFG, and find patterns.
// Depends: re_disasm, re_disasm_cfg, re_dbg_find, re_test.
#include "features/disasm/re_disasm.h"
#include "features/search/re_dbg_find.h"
#include "re_test.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_enrich_call_jcc_ret(void) {
    /* e8 10 00 00 00 = call +0x10; 74 02 = jz +2; c3 = ret */
    uint8_t code[] = {0xE8, 0x10, 0x00, 0x00, 0x00, 0x74, 0x02, 0xC3};
    re_disasm_insn_t lines[8];
    size_t n = 0;
    RE_CHECK_EQ_U(re_disasm_x64_many(0x1000, code, sizeof(code), 8, lines, &n), RE_OK);
    RE_CHECK(n >= 3);
    re_disasm_enrich(&lines[0]);
    RE_CHECK((lines[0].flags & RE_DISASM_F_CALL) != 0);
    RE_CHECK_EQ_U(lines[0].target, 0x1015ull);
    re_disasm_enrich(&lines[1]);
    RE_CHECK((lines[1].flags & RE_DISASM_F_JCC) != 0);
    re_disasm_enrich(&lines[2]);
    RE_CHECK((lines[2].flags & RE_DISASM_F_RET) != 0);
}

static void test_find_parse(void) {
    re_dbg_find_pat_t p;
    RE_CHECK_EQ_U(re_dbg_find_parse("48 89 ??", &p), RE_OK);
    RE_CHECK_EQ_U(p.n, 3);
    RE_CHECK_EQ_U(p.bytes[0], 0x48);
    RE_CHECK_EQ_U(p.mask[2], 0);
    RE_CHECK_EQ_U(re_dbg_find_parse("\"Hi\"", &p), RE_OK);
    RE_CHECK_EQ_U(p.n, 2);
    RE_CHECK_EQ_U(p.bytes[0], (uint8_t)'H');
    RE_CHECK_EQ_U(re_dbg_find_parse("u32:0x01020304", &p), RE_OK);
    RE_CHECK_EQ_U(p.n, 4);
    RE_CHECK_EQ_U(p.bytes[0], 0x04);
    RE_CHECK_EQ_U(p.bytes[3], 0x01);
}

static void test_cfg_blocks(void) {
    /* entry: jz +2; nop; nop; ret  => leaders at entry and fall/taken */
    uint8_t code[] = {0x74, 0x02, 0x90, 0x90, 0xC3};
    re_disasm_insn_t lines[8];
    size_t n = 0;
    uint64_t leaders_check = 0;
    RE_CHECK_EQ_U(re_disasm_x64_many(0x2000, code, sizeof(code), 8, lines, &n), RE_OK);
    RE_CHECK(n >= 3);
    for (size_t i = 0; i < n; i++) {
        re_disasm_enrich(&lines[i]);
        if (lines[i].flags & RE_DISASM_F_JCC)
            leaders_check = lines[i].target;
    }
    RE_CHECK(leaders_check == 0x2004ull || leaders_check == 0x2002ull + 2ull);
    /* Offline CFG shape: at least one jcc edge target inside window. */
    RE_CHECK((lines[0].flags & RE_DISASM_F_JCC) != 0);
    RE_CHECK_EQ_U(lines[0].target, 0x2004ull);
}

int main(void) {
    test_enrich_call_jcc_ret();
    test_find_parse();
    test_cfg_blocks();
    return re_test_report("disasm_p2");
}
