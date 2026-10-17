// test_cfg.c - themes, keybindings, and F-key decode for Pack B.
// Module: test (C11).
// Owns: assertions for re_cfg, re_theme, re_keys, and CSI Fn mapping.
// Depends: re_utils, re_keys, re_input. Writes only via re_test.h.
#include "re_test.h"

#include "utils/cfg/re_cfg.h"
#include "utils/cfg/re_theme.h"
#include "utils/tui/re_tui.h"
#include "cli/screen/re_input.h"
#include "cli/screen/re_keys.h"

#include <string.h>

int re_test_count = 0;
int re_test_fail = 0;

static void test_sgr_and_style_id(void) {
    char esc[RE_CFG_SGR_MAX];
    RE_CHECK(re_cfg_sgr_esc(esc, sizeof(esc), "1;36", 4));
    RE_CHECK(strcmp(esc, "\x1b[1;36m") == 0);
    RE_CHECK(re_cfg_style_id("accent", 6) == (int)RE_ST_ACCENT);
    RE_CHECK(re_cfg_style_id("nope", 4) == -1);
    RE_CHECK(re_cfg_sniff("  {\"a\":1}", 8) == RE_CFG_KIND_JSON);
    RE_CHECK(re_cfg_sniff("[keys]\n", 7) == RE_CFG_KIND_INI);
}

static void test_theme_json(void) {
    static const char *json =
        "{\"schema\":\"antinomia/1\",\"name\":\"hi\",\"styles\":{\"accent\":\"1;35\"}}";
    re_theme_t t;
    RE_CHECK(re_theme_load_text(&t, json, strlen(json), RE_CFG_KIND_JSON));
    RE_CHECK(strcmp(t.name, "hi") == 0);
    RE_CHECK(strcmp(t.on[RE_ST_ACCENT], "\x1b[1;35m") == 0);
    re_theme_apply(&t);
    RE_CHECK(strcmp(re_tui_style_on(RE_ST_ACCENT), "\x1b[1;35m") == 0);
    re_tui_styles_reset();
    RE_CHECK(strcmp(re_tui_style_on(RE_ST_ACCENT), "\x1b[1;36m") == 0);
}

static void test_theme_ini(void) {
    static const char *ini = "[theme]\nname=ini\n[styles]\nbad=1;31\n";
    re_theme_t t;
    RE_CHECK(re_theme_load_text(&t, ini, strlen(ini), RE_CFG_KIND_INI));
    RE_CHECK(strcmp(t.name, "ini") == 0);
    RE_CHECK(strcmp(t.on[RE_ST_BAD], "\x1b[1;31m") == 0);
}

static void feed_key(const char *bytes, size_t n, re_ev_t *ev) {
    re_input_t in;
    re_input_init(&in);
    re_input_feed(&in, (const uint8_t *)bytes, n);
    RE_CHECK(re_input_next(&in, ev));
    RE_CHECK(ev->kind == RE_EV_KEY);
}

static void test_fkey_decode(void) {
    re_ev_t ev;
    /* CSI 18~ = F7, CSI 19~ = F8, CSI 20~ = F9, CSI 15~ = F5, CSI 24~ = F12 */
    feed_key("\x1b[18~", 5, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F7);
    feed_key("\x1b[19~", 5, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F8);
    feed_key("\x1b[20~", 5, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F9);
    feed_key("\x1b[15~", 5, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F5);
    feed_key("\x1b[24~", 5, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F12);
    feed_key("\x1bOP", 3, &ev);
    RE_CHECK_EQ_U(ev.key, RE_KEY_F1);
}

static void test_keys_defaults(void) {
    re_keys_t k;
    re_ev_t ev;
    re_keys_defaults(&k);
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_EV_KEY;
    ev.key = RE_KEY_F9;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_RUN);
    ev.key = RE_KEY_F7;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_STEP_INTO);
    ev.key = RE_KEY_F8;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_STEP_OVER);
    ev.key = RE_KEY_F2;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_TOGGLE_BP);
    ev.key = RE_KEY_F12;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_PAUSE);
    ev.key = RE_KEY_TAB;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_FOCUS_NEXT);
    ev.key = 'q';
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_QUIT);
}

static void test_keys_json_override(void) {
    static const char *json =
        "{\"bindings\":[{\"key\":\"x\",\"action\":\"run\"},{\"key\":\"F7\",\"action\":\"pause\"}]}";
    re_keys_t k;
    re_ev_t ev;
    RE_CHECK(re_keys_load_text(&k, json, strlen(json), RE_CFG_KIND_JSON));
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_EV_KEY;
    ev.key = 'x';
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_RUN);
    ev.key = RE_KEY_F7;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_PAUSE);
}

static void test_keys_ini(void) {
    static const char *ini = "[keys]\nF9=step_into\nz=quit\n";
    re_keys_t k;
    re_ev_t ev;
    RE_CHECK(re_keys_load_text(&k, ini, strlen(ini), RE_CFG_KIND_INI));
    memset(&ev, 0, sizeof(ev));
    ev.kind = RE_EV_KEY;
    ev.key = RE_KEY_F9;
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_STEP_INTO);
    ev.key = 'z';
    RE_CHECK(re_keys_match(&k, &ev) == RE_ACT_QUIT);
}

int main(void) {
    test_sgr_and_style_id();
    test_theme_json();
    test_theme_ini();
    test_fkey_decode();
    test_keys_defaults();
    test_keys_json_override();
    test_keys_ini();
    return re_test_fail ? 1 : 0;
}
