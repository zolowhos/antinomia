// re_gui_menu.c - dropdowns and action buttons for the debugger chrome.
// Module: cli (C11).
// Owns: item lists, chrome paint, and the click that runs one.
// Depends: re_gui_menu.h, re_screen, re_ui, re_tui, re_keys.
#include "cli/screen/menu/re_gui_menu.h"

#include "utils/tui/re_tui.h"

typedef struct {
    re_gui_menu_ctx_t *c;
    re_ui_t *ui;
    int menu;
    int item;
    re_act_t act;
} menu_arg_t;

static menu_arg_t g_arg[24];
static size_t g_narg;

static const char *const kBar[] = {"File", "Edit", "Jump", "Search", "Options", "Help"};
static const char *const kBtns[] = {"Run", "Step", "Over", "Pause", "BP"};
static const re_act_t kBtnAct[] = {RE_ACT_RUN, RE_ACT_STEP_INTO, RE_ACT_STEP_OVER, RE_ACT_PAUSE,
                                   RE_ACT_TOGGLE_BP};

static bool menu_owns_zone(const re_gui_menu_ctx_t *c, int menu, uint8_t zone) {
    uint8_t z0;
    uint8_t n;
    if (!c || zone == RE_SCREEN_ZONE_NONE)
        return false;
    if (zone == (uint8_t)(c->bar + menu))
        return true;
    if (!c->open || *c->open != menu || !c->zone || !c->nitems)
        return false;
    z0 = *c->zone;
    n = *c->nitems;
    if (z0 == RE_SCREEN_ZONE_NONE || n == 0)
        return false;
    return zone >= z0 && zone < (uint8_t)(z0 + n);
}

static const char *const kEdit[] = {"Next pane", "Previous pane"};
static const char *const kJump[] = {"Disasm", "Log"};
static const char *const kSearch[] = {"Maps", "Threads", "Breakpoints"};
static const char *const kOptions[] = {"Color"};
static const char *const kHelp[] = {"Keys", "Quit"};

static const char *const *items_of(int menu, size_t *n) {
    *n = 0;
    if (menu == 1) {
        *n = 2;
        return kEdit;
    }
    if (menu == 2) {
        *n = 2;
        return kJump;
    }
    if (menu == 3) {
        *n = 3;
        return kSearch;
    }
    if (menu == 4) {
        *n = 1;
        return kOptions;
    }
    if (menu == 5) {
        *n = 2;
        return kHelp;
    }
    return NULL;
}

static uint16_t menu_x(int menu) {
    uint16_t x = 1;
    int i;
    for (i = 0; i < menu && i < 6; i++)
        x = (uint16_t)(x + 1u + re_tui_cols(kBar[i]) + 2u + 1u);
    return x;
}

static void set_focus(re_gui_menu_ctx_t *c, size_t pane) {
    if (c->focus && pane < c->n_panes)
        *c->focus = pane;
}

static void run_item(re_gui_menu_ctx_t *c, int menu, int item) {
    if (menu == 1 && item == 0 && c->focus && c->n_panes)
        *c->focus = (*c->focus + 1u) % c->n_panes;
    if (menu == 1 && item == 1 && c->focus && c->n_panes)
        *c->focus = (*c->focus + c->n_panes - 1u) % c->n_panes;
    if (menu == 2 && item == 0)
        set_focus(c, 0); /* disasm */
    if (menu == 2 && item == 1)
        set_focus(c, 8); /* log */
    if (menu == 3 && item == 0)
        set_focus(c, 4); /* maps */
    if (menu == 3 && item == 1)
        set_focus(c, 5); /* threads */
    if (menu == 3 && item == 2)
        set_focus(c, 6); /* breakpoints */
    if (menu == 4 && item == 0 && c->color)
        *c->color = !*c->color;
    if (menu == 5 && item == 0 && c->log_fn)
        c->log_fn(c->log_user, "F9 run F7/F8 step F2 bp F12 pause Tab focus q quit");
    if (menu == 5 && item == 1 && c->quit)
        *c->quit = true;
    if (c->repaint)
        *c->repaint = true;
    if (c->open)
        *c->open = -1;
}

static void on_bar(void *user) {
    menu_arg_t *a = user;
    if (!a || !a->c || !a->c->open)
        return;
    *a->c->open = *a->c->open == a->menu ? -1 : a->menu;
    if (a->c->repaint)
        *a->c->repaint = true;
}

static void on_file(void *user) {
    menu_arg_t *a = user;
    if (!a || !a->c)
        return;
    if (a->c->open)
        *a->c->open = -1;
    if (a->c->log_fn)
        a->c->log_fn(a->c->log_user, a->c->path && a->c->path[0] ? a->c->path : "(no path)");
    if (a->c->repaint)
        *a->c->repaint = true;
}

static void on_hover(void *user, bool over) {
    menu_arg_t *a = user;
    if (!a || !a->c || !a->c->open || !a->ui)
        return;
    if (over) {
        if (*a->c->open == a->menu)
            return;
        *a->c->open = a->menu;
        if (a->c->repaint)
            *a->c->repaint = true;
        return;
    }
    if (*a->c->open != a->menu)
        return;
    if (menu_owns_zone(a->c, a->menu, a->ui->hot))
        return;
    *a->c->open = -1;
    if (a->c->repaint)
        *a->c->repaint = true;
}

static void on_item(void *user) {
    menu_arg_t *a = user;
    if (a && a->c)
        run_item(a->c, a->menu, a->item);
}

static void on_btn(void *user) {
    menu_arg_t *a = user;
    if (!a || !a->c || !a->c->pending)
        return;
    *a->c->pending = a->act;
    if (a->c->open)
        *a->c->open = -1;
    if (a->c->repaint)
        *a->c->repaint = true;
}

static menu_arg_t *arg(re_gui_menu_ctx_t *c, re_ui_t *ui, int menu, int item, re_act_t act) {
    menu_arg_t *a;
    if (g_narg >= 24)
        return NULL;
    a = &g_arg[g_narg++];
    a->c = c;
    a->ui = ui;
    a->menu = menu;
    a->item = item;
    a->act = act;
    return a;
}

static uint16_t bracket_item(re_screen_t *s, uint16_t y, uint16_t x, uint16_t limit,
                             const char *label, uint8_t zone) {
    if (x >= limit)
        return x;
    re_screen_put(s, y, x, "<", (uint8_t)RE_ST_NONE, zone);
    x = re_screen_put_run(s, y, (uint16_t)(x + 1u), limit, label ? label : "", (uint8_t)RE_ST_NONE,
                          zone);
    if (x < limit) {
        re_screen_put(s, y, x, ">", (uint8_t)RE_ST_NONE, zone);
        x++;
    }
    return x;
}

static void draw_menu_row(re_screen_t *s, re_gui_menu_ctx_t *c) {
    uint16_t last = (uint16_t)(s->cols > 0u ? s->cols - 1u : 0u);
    size_t mw = (c->mark && *c->mark) ? re_tui_cols(c->mark) + 2u : 0u;
    uint16_t mark_x = last;
    uint16_t cx = 1;
    uint8_t first = RE_SCREEN_ZONE_NONE;
    size_t i;
    if (mw && mw + 2u < last)
        mark_x = (uint16_t)(last - 1u - mw);
    re_screen_fill(s, 0, 0, s->cols, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    for (i = 0; i < 6u && cx + 4u < mark_x; i++) {
        size_t lc = re_tui_cols(kBar[i]);
        uint8_t zone;
        if (cx + 1u + lc + 2u >= mark_x)
            break;
        zone = re_screen_zone(s);
        if (first == RE_SCREEN_ZONE_NONE)
            first = zone;
        cx = bracket_item(s, 0, cx, mark_x, kBar[i], zone);
        if (cx < mark_x) {
            re_screen_put(s, 0, cx, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
            cx++;
        }
    }
    c->bar = first;
    if (c->path && *c->path && cx + 2u < mark_x)
        cx =
            re_screen_put_run(s, 0, cx, mark_x, c->path, (uint8_t)RE_ST_LABEL, RE_SCREEN_ZONE_NONE);
    if (mw && mark_x < last)
        (void)bracket_item(s, 0, mark_x, last, c->mark, RE_SCREEN_ZONE_NONE);
}

static void draw_btn_row(re_screen_t *s, re_gui_menu_ctx_t *c) {
    uint16_t cx = 1;
    uint16_t last = s->cols;
    size_t i;
    uint8_t first = RE_SCREEN_ZONE_NONE;
    uint8_t n = 0;
    if (s->rows < 2u)
        return;
    re_screen_fill(s, 1, 0, s->cols, 1, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
    for (i = 0; i < 5u && cx + 4u < last; i++) {
        uint8_t zone = re_screen_zone(s);
        if (first == RE_SCREEN_ZONE_NONE)
            first = zone;
        n++;
        cx = bracket_item(s, 1, cx, last, kBtns[i], zone);
        if (cx + 1u < last) {
            re_screen_put(s, 1, cx, " ", (uint8_t)RE_ST_NONE, RE_SCREEN_ZONE_NONE);
            cx++;
        }
    }
    if (c->btn_zone)
        *c->btn_zone = first;
    if (c->btn_n)
        *c->btn_n = n;
}

void re_gui_chrome_draw(re_screen_t *s, re_gui_menu_ctx_t *c) {
    if (!s || !c || s->rows < RE_GUI_CHROME_TOP)
        return;
    c->bar = RE_SCREEN_ZONE_NONE;
    if (c->btn_zone)
        *c->btn_zone = RE_SCREEN_ZONE_NONE;
    if (c->btn_n)
        *c->btn_n = 0;
    draw_menu_row(s, c);
    draw_btn_row(s, c);
}

void re_gui_menu_draw(re_screen_t *s, re_gui_menu_ctx_t *c) {
    size_t n = 0, i;
    const char *const *items;
    uint16_t x, w = 0;
    if (!s || !c || !c->open || *c->open < 1)
        return;
    items = items_of(*c->open, &n);
    if (!items)
        return;
    x = menu_x(*c->open);
    for (i = 0; i < n; i++)
        if (re_tui_cols(items[i]) > w)
            w = (uint16_t)re_tui_cols(items[i]);
    if (c->zone)
        *c->zone = RE_SCREEN_ZONE_NONE;
    if (c->nitems)
        *c->nitems = 0;
    for (i = 0; i < n && (uint16_t)(2u + i) < s->rows; i++) {
        uint8_t zone = re_screen_zone(s);
        if (c->zone && *c->zone == RE_SCREEN_ZONE_NONE)
            *c->zone = zone;
        if (c->nitems)
            (*c->nitems)++;
        re_screen_fill(s, (uint16_t)(1u + i), x, (uint16_t)(w + 2u), 1, " ", (uint8_t)RE_ST_LABEL,
                       zone);
        re_screen_put_run(s, (uint16_t)(1u + i), (uint16_t)(x + 1u), (uint16_t)(x + 1u + w),
                          items[i], (uint8_t)RE_ST_LABEL, zone);
    }
}

void re_gui_menu_bind(re_ui_t *u, re_gui_menu_ctx_t *c) {
    int menu;
    size_t i, n = 0;
    g_narg = 0;
    if (!u || !c || c->bar == RE_SCREEN_ZONE_NONE)
        return;
    {
        menu_arg_t *a = arg(c, u, 0, -1, RE_ACT_NONE);
        if (a)
            re_ui_bind(u, c->bar, on_file, NULL, a);
    }
    for (menu = 1; menu <= 5; menu++) {
        menu_arg_t *a = arg(c, u, menu, -1, RE_ACT_NONE);
        if (a)
            re_ui_bind(u, (uint8_t)(c->bar + menu), on_bar, on_hover, a);
    }
    if (c->open && *c->open >= 1 && c->zone && *c->zone != RE_SCREEN_ZONE_NONE) {
        items_of(*c->open, &n);
        for (i = 0; i < n && i < (c->nitems ? *c->nitems : 0u); i++) {
            menu_arg_t *a = arg(c, u, *c->open, (int)i, RE_ACT_NONE);
            if (a)
                re_ui_bind(u, (uint8_t)(*c->zone + i), on_item, on_hover, a);
        }
    }
    if (c->btn_zone && *c->btn_zone != RE_SCREEN_ZONE_NONE && c->btn_n) {
        for (i = 0; i < *c->btn_n && i < 5u; i++) {
            menu_arg_t *a = arg(c, u, -1, (int)i, kBtnAct[i]);
            if (a)
                re_ui_bind(u, (uint8_t)(*c->btn_zone + i), on_btn, NULL, a);
        }
    }
}
