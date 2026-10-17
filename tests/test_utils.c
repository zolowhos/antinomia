// test_utils.c - verifies the utility layer against published test vectors.
// Module: test (C11).
// Owns: correctness checks for arena, string, container, hash, crc and regex code.
// Depends: re_utils only. Prints to stdout, which is fine for a test binary.
#include "utils/text/re_util.h"
#include "re_test.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_bits(void) {
    RE_CHECK_EQ_U(re_bits_popcount(0), 0);
    RE_CHECK_EQ_U(re_bits_popcount(0xffffffffffffffffULL), 64);
    RE_CHECK_EQ_U(re_bits_ctz64(8), 3);
    RE_CHECK(re_align_up(5, 8) == 8);
    RE_CHECK(re_bits_is_pow2(64));
}

static void test_hex(void) {
    const uint8_t data[4] = {0xde, 0xad, 0xbe, 0xef};
    char out[16];
    re_hex_encode(out, data, 4);
    RE_CHECK(strcmp(out, "deadbeef") == 0);
    uint8_t back[4];
    size_t n = 0;
    RE_CHECK(re_hex_decode(back, sizeof(back), "deadbeef", 8, &n, false));
    RE_CHECK_EQ_U(n, 4);
    RE_CHECK(memcmp(back, data, 4) == 0);
}

static void test_arena(void) {
    re_arena_t a;
    re_arena_init(&a, 1024);
    void *p = re_arena_alloc(&a, 32);
    RE_CHECK(p != NULL);
    re_arena_reset(&a);
    void *q = re_arena_alloc(&a, 16);
    RE_CHECK(q != NULL);
    re_arena_free(&a);
}

static void test_str(void) {
    RE_CHECK(re_str_eq_cstr(re_str("abc"), "abc"));
    RE_CHECK(!re_str_eq_cstr(re_str("abc"), "abd"));
}

static void test_err_json(void) {
    re_err_privilege_t priv;
    RE_CHECK(strcmp(re_err_json_code(RE_OK), "ok") == 0);
    RE_CHECK(strcmp(re_err_json_code(RE_E_USAGE), "usage") == 0);
    RE_CHECK(strcmp(re_err_json_code(RE_E_PRIVILEGE), "privilege") == 0);
    RE_CHECK(strcmp(re_err_json_code(RE_E_UNSUPPORTED), "unsupported") == 0);
    RE_CHECK(strcmp(re_err_json_code(RE_E_IO), "io") == 0);
    RE_CHECK(strcmp(re_err_json_code(RE_E_NOTFOUND), "notfound") == 0);
    RE_CHECK(re_err_exit_code(RE_E_PRIVILEGE) == 6);
    re_err_privilege_fill("attach", &priv);
    RE_CHECK(strcmp(priv.kind, "attach") == 0);
    re_err_privilege_fill("init", &priv);
    RE_CHECK(strcmp(priv.kind, "launch") == 0);
    re_err_privilege_fill("launch", &priv);
    RE_CHECK(strcmp(priv.kind, "launch") == 0);
    re_err_privilege_fill("other", &priv);
    RE_CHECK(strcmp(priv.kind, "access") == 0);
#if defined(_WIN32)
    RE_CHECK(strcmp(priv.platform, "windows") == 0);
    RE_CHECK(strcmp(priv.hint, "SeDebugPrivilege") == 0);
#else
    RE_CHECK(strcmp(priv.platform, "linux") == 0);
    RE_CHECK(strcmp(priv.hint, "/proc/sys/kernel/yama/ptrace_scope") == 0);
#endif
}

static void test_seed_shuffle(void) {
    uint32_t a[8], b[8], c[8];
    re_rng_t ra, rb, rc;
    size_t i;
    for (i = 0; i < 8; i++) {
        a[i] = (uint32_t)i;
        b[i] = (uint32_t)i;
        c[i] = (uint32_t)i;
    }
    re_rng_init(&ra, 42);
    re_rng_init(&rb, 42);
    re_rng_init(&rc, 7);
    re_rng_shuffle_u32(&ra, a, 8);
    re_rng_shuffle_u32(&rb, b, 8);
    re_rng_shuffle_u32(&rc, c, 8);
    RE_CHECK(memcmp(a, b, sizeof(a)) == 0);
    RE_CHECK(memcmp(a, c, sizeof(a)) != 0);
}

int main(void) {
    re_crc_init();
    test_bits();
    test_hex();
    test_arena();
    test_str();
    test_err_json();
    test_seed_shuffle();
    return re_test_report("utils");
}
