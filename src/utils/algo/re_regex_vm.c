// re_regex_vm.c - public regex API, program finalization and the backtracking VM.
// Module: util (C11).
// Owns: compile, the step and depth caps, search, anchored match, literal detect.
// Depends: re_regex_priv.h only. No I/O. Untrusted patterns cannot run unbounded.
#include "utils/algo/re_regex_priv.h"

#include <string.h>

#define RX_STEP_LIMIT 200000u

static bool char_eq(const re_rx_t *rx, unsigned char a, unsigned char b) {
    if (a == b)
        return true;
    if (!rx->icase)
        return false;
    return re_char_lower((char)a) == re_char_lower((char)b);
}

static bool step_ok(const re_rx_t *rx, const rx_i_t *in, re_str_t t, size_t sp) {
    switch (in->op) {
        case RXI_CHAR:
            return sp < t.n && char_eq(rx, in->ch, (unsigned char)t.p[sp]);
        case RXI_ANY:
            return sp < t.n && t.p[sp] != '\n';
        case RXI_CLASS:
            return sp < t.n && re_rx_cls_get(rx->classes[in->cls], (unsigned char)t.p[sp]);
        case RXI_BOL:
            return sp == 0;
        case RXI_EOL:
            return sp == t.n;
        default:
            return false;
    }
}

// Flat program walk with an explicit backtrack stack, so there is no recursion
// and no dependence on the C stack. Both caps keep worst case time bounded,
// which matters because patterns and haystacks both come from untrusted files.
static bool vm_run(const re_rx_t *rx, re_str_t t, size_t start, size_t *end) {
    if (!rx->n_inst)
        return false;
    const rx_i_t *pc = &rx->prog[0];
    size_t sp = start;
    size_t depth = 0;
    unsigned steps = 0;
    for (;;) {
        if (++steps > RX_STEP_LIMIT)
            return false;
        if (pc->op == RXI_MATCH) {
            *end = sp;
            return true;
        }
        if (pc->op == RXI_JMP) {
            pc = pc->x;
            continue;
        }
        if (pc->op == RXI_SPLIT) {
            if (depth >= RE_RX_MAX_STACK)
                return false;
            rx->stack[depth].pc = pc->y;
            rx->stack[depth].sp = sp;
            depth++;
            pc = pc->x;
            continue;
        }
        if (pc->op == RXI_BOL || pc->op == RXI_EOL) {
            // Zero width assertions advance the program but never the position.
            if (step_ok(rx, pc, t, sp)) {
                pc = pc->x;
                continue;
            }
        } else if (step_ok(rx, pc, t, sp)) {
            pc = pc->x;
            sp++;
            continue;
        }
        if (depth == 0)
            return false;
        depth--;
        pc = rx->stack[depth].pc;
        sp = rx->stack[depth].sp;
    }
}

// A pattern with no unescaped metacharacter can use re_str_find instead of the
// engine, which is the common case when filtering strings out of a binary.
bool re_rx_is_literal(const re_rx_t *rx, const char **out, size_t *out_n) {
    if (!rx->literal)
        return false;
    if (out)
        *out = rx->lit;
    if (out_n)
        *out_n = rx->lit_n;
    return true;
}

static bool detect_literal(const char *p, const char *end) {
    for (const char *q = p; q < end; q++) {
        if (*q == '\\') {
            q++;
            if (q >= end)
                return false;
            continue;
        }
        if (*q == '.' || *q == '[' || *q == ']' || *q == '(' || *q == ')' || *q == '*' ||
            *q == '+' || *q == '?' || *q == '{' || *q == '}' || *q == '|' || *q == '^' || *q == '$')
            return false;
    }
    return true;
}

// Fill in the implicit fall-through targets and point every recorded exit at the
// terminating MATCH, then copy the character classes into the compiled record.
static bool finalize(re_rx_t *rx, parse_t *ps) {
    size_t m = re_rx_emit(rx, RXI_MATCH);
    if (m == RX_FAIL)
        return false;
    for (unsigned i = 0; i < ps->n_exits; i++)
        rx->prog[ps->exits[i]].y = &rx->prog[m];
    for (size_t i = 0; i < rx->n_inst; i++) {
        rx_i_t *in = &rx->prog[i];
        if (!in->x && in->op != RXI_MATCH && in->op != RXI_JMP && in->op != RXI_SPLIT)
            in->x = &rx->prog[i + 1];
    }
    rx->match_index = m;
    for (unsigned c = 0; c < ps->n_classes && c < RE_RX_MAX_CLASS; c++)
        memcpy(rx->classes[c], ps->classes[c], 32);
    return true;
}

re_rx_t *re_rx_compile(re_arena_t *a, const char *pattern, const char *flags, re_err_t *err) {
    if (err)
        err->code = RE_OK;
    if (!pattern) {
        if (err)
            re_err_set(err, RE_E_USAGE, __FILE__, __LINE__, "regex: null pattern");
        return NULL;
    }
    re_rx_t *rx = (re_rx_t *)re_arena_calloc(a, 1, sizeof(*rx));
    if (!rx)
        return NULL;
    rx->a = a;
    rx->prog = (rx_i_t *)re_arena_calloc(a, RE_RX_MAX_INST, sizeof(*rx->prog));
    rx->stack = (rx_st_t *)re_arena_calloc(a, RE_RX_MAX_STACK, sizeof(*rx->stack));
    if (!rx->prog || !rx->stack) {
        if (err)
            re_err_set(err, RE_E_NOMEM, __FILE__, __LINE__, "regex: out of memory");
        return NULL;
    }
    parse_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.p = pattern;
    ps.end = pattern + strlen(pattern);
    ps.a = a;
    ps.err = err;
    for (const char *f = flags; f && *f; f++) {
        if (*f == 'i')
            ps.icase = true;
        else if (err && err->code == RE_OK)
            re_err_setf(err, RE_E_USAGE, __FILE__, __LINE__, "regex: unknown flag '%c'", *f);
    }
    if (err && err->code != RE_OK)
        return NULL;
    rx->icase = ps.icase;
    rx->literal = detect_literal(pattern, ps.end);
    if (rx->literal) {
        rx->lit = pattern;
        rx->lit_n = (size_t)(ps.end - ps.p);
    }
    rx_n_t *root = re_rx_parse(&ps);
    if (!root)
        return NULL;
    if (re_rx_gen(rx, root, &ps) == RX_FAIL) {
        if (err)
            re_err_set(err, RE_E_USAGE, __FILE__, __LINE__, "regex: pattern too complex");
        return NULL;
    }
    if (!finalize(rx, &ps)) {
        if (err)
            re_err_set(err, RE_E_USAGE, __FILE__, __LINE__, "regex: pattern too complex");
        return NULL;
    }
    return rx;
}

void re_rx_free(re_rx_t *rx) {
    (void)rx;
}

bool re_rx_match_at(const re_rx_t *rx, re_str_t text, size_t at, size_t *end) {
    if (!rx || at > text.n)
        return false;
    return vm_run(rx, text, at, end);
}

// Literal patterns skip the engine entirely, which is the common case when an
// agent filters strings out of a binary.
static bool search_literal(const re_rx_t *rx, re_str_t text, size_t *start, size_t *end) {
    re_str_t lit = re_strn(rx->lit, rx->lit_n);
    if (lit.n == 0) {
        if (start)
            *start = 0;
        *end = 0;
        return true;
    }
    if (!rx->icase) {
        long at = re_str_find(text, lit, 0);
        if (at < 0)
            return false;
        if (start)
            *start = (size_t)at;
        *end = (size_t)at + lit.n;
        return true;
    }
    for (size_t i = 0; i + lit.n <= text.n; i++) {
        size_t k = 0;
        while (k < lit.n && re_char_lower(text.p[i + k]) == re_char_lower(lit.p[k]))
            k++;
        if (k == lit.n) {
            if (start)
                *start = i;
            *end = i + lit.n;
            return true;
        }
    }
    return false;
}

bool re_rx_search(const re_rx_t *rx, re_str_t text, size_t *start, size_t *end) {
    if (!rx)
        return false;
    if (rx->literal)
        return search_literal(rx, text, start, end);
    for (size_t s = 0; s <= text.n; s++) {
        if (vm_run(rx, text, s, end)) {
            if (start)
                *start = s;
            return true;
        }
    }
    return false;
}
