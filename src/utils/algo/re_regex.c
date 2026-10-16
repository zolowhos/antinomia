// re_regex.c - regex parser and code generator producing a flat instruction program.
// Module: util (C11).
// Owns: recursive descent parse into a node tree, then emission into re_rx.prog.
// Depends: re_regex_priv.h only. No I/O, no globals, all state in parse_t.
#include "utils/algo/re_regex_priv.h"

#include <limits.h>
#include <string.h>

void re_rx_fail(parse_t *ps, const char *msg) {
    if (ps->err && ps->err->code == RE_OK)
        re_err_setf(ps->err, RE_E_USAGE, __FILE__, __LINE__, "regex: %s", msg);
}

void re_rx_cls_set(unsigned char *m, unsigned char c) {
    m[c >> 3] |= (unsigned char)(1u << (c & 7u));
}

bool re_rx_cls_get(const unsigned char *m, unsigned char c) {
    return (m[c >> 3] & (1u << (c & 7u))) != 0;
}

void re_rx_cls_range(unsigned char *m, unsigned char lo, unsigned char hi) {
    for (unsigned i = lo; i <= hi; i++)
        re_rx_cls_set(m, (unsigned char)i);
}

rx_n_t *re_rx_node(parse_t *ps, ntype_t t) {
    rx_n_t *n = (rx_n_t *)re_arena_calloc(ps->a, 1, sizeof(*n));
    if (n)
        n->type = t;
    return n;
}

static rx_n_t *parse_alt(parse_t *ps);

static void add_escape_class(unsigned char *m, char e) {
    if (e == 'd') {
        re_rx_cls_range(m, '0', '9');
    } else if (e == 'w') {
        re_rx_cls_range(m, 'a', 'z');
        re_rx_cls_range(m, 'A', 'Z');
        re_rx_cls_range(m, '0', '9');
        re_rx_cls_set(m, '_');
    } else {
        re_rx_cls_set(m, ' ');
        re_rx_cls_set(m, '\t');
        re_rx_cls_set(m, '\n');
        re_rx_cls_set(m, '\r');
    }
}

static rx_n_t *parse_class(parse_t *ps) {
    rx_n_t *n = re_rx_node(ps, N_CLASS);
    if (!n)
        return NULL;
    if (ps->n_classes >= RE_RX_MAX_CLASS) {
        re_rx_fail(ps, "too many classes");
        return NULL;
    }
    unsigned char *m = ps->classes[ps->n_classes];
    bool neg = false;
    if (ps->p < ps->end && *ps->p == '^') {
        neg = true;
        ps->p++;
    }
    bool first = true;
    while (ps->p < ps->end && (*ps->p != ']' || first)) {
        first = false;
        unsigned char lo = (unsigned char)*ps->p++;
        if (lo == '\\' && ps->p < ps->end) {
            unsigned char e = (unsigned char)*ps->p++;
            if (e == 'd' || e == 'w' || e == 's') {
                add_escape_class(m, (char)e);
                continue;
            }
            lo = e;
        }
        if (ps->p + 1 < ps->end && *ps->p == '-' && ps->p[1] != ']') {
            ps->p++;
            re_rx_cls_range(m, lo, (unsigned char)*ps->p++);
        } else {
            re_rx_cls_set(m, lo);
        }
    }
    if (ps->p >= ps->end) {
        re_rx_fail(ps, "missing close bracket");
        return NULL;
    }
    ps->p++;
    if (neg) {
        for (int i = 0; i < 32; i++)
            m[i] = (unsigned char)~m[i];
    }
    n->cls = ps->n_classes++;
    return n;
}

static rx_n_t *parse_atom(parse_t *ps) {
    if (ps->p >= ps->end) {
        re_rx_fail(ps, "unexpected end of pattern");
        return NULL;
    }
    char c = *ps->p++;
    if (c == '(') {
        rx_n_t *inner = parse_alt(ps);
        if (!inner)
            return NULL;
        if (ps->p >= ps->end || *ps->p != ')') {
            re_rx_fail(ps, "missing close paren");
            return NULL;
        }
        ps->p++;
        return inner;
    }
    if (c == '[')
        return parse_class(ps);
    if (c == '.')
        return re_rx_node(ps, N_ANY);
    if (c == '^')
        return re_rx_node(ps, N_BOL);
    if (c == '$')
        return re_rx_node(ps, N_EOL);
    if (c == '*' || c == '+' || c == '?') {
        re_rx_fail(ps, "repetition with nothing to repeat");
        return NULL;
    }
    if (c == '\\') {
        if (ps->p >= ps->end) {
            re_rx_fail(ps, "trailing backslash");
            return NULL;
        }
        char e = *ps->p++;
        if (e == 'd' || e == 'w' || e == 's') {
            rx_n_t *n = re_rx_node(ps, N_CLASS);
            if (!n)
                return NULL;
            if (ps->n_classes >= RE_RX_MAX_CLASS) {
                re_rx_fail(ps, "too many classes");
                return NULL;
            }
            add_escape_class(ps->classes[ps->n_classes], e);
            n->cls = ps->n_classes++;
            return n;
        }
        rx_n_t *n = re_rx_node(ps, N_CHAR);
        if (n)
            n->ch = (unsigned char)e;
        return n;
    }
    rx_n_t *n = re_rx_node(ps, N_CHAR);
    if (n)
        n->ch = (unsigned char)c;
    return n;
}

static bool parse_count(parse_t *ps, unsigned *min, unsigned *max) {
    const char *save = ps->p;
    ps->p++;
    *min = 0;
    while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
        *min = *min * 10u + (unsigned)(*ps->p++ - '0');
    *max = *min;
    if (ps->p < ps->end && *ps->p == ',') {
        ps->p++;
        if (ps->p < ps->end && *ps->p == '}') {
            *max = UINT_MAX;
        } else {
            *max = 0;
            while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
                *max = *max * 10u + (unsigned)(*ps->p++ - '0');
        }
    }
    if (ps->p >= ps->end || *ps->p != '}') {
        ps->p = save;
        return false;
    }
    ps->p++;
    return true;
}

static rx_n_t *parse_rep(parse_t *ps) {
    rx_n_t *atom = parse_atom(ps);
    if (!atom || ps->p >= ps->end)
        return atom;
    char c = *ps->p;
    unsigned min;
    unsigned max;
    if (c == '*') {
        min = 0;
        max = UINT_MAX;
        ps->p++;
    } else if (c == '+') {
        min = 1;
        max = UINT_MAX;
        ps->p++;
    } else if (c == '?') {
        min = 0;
        max = 1;
        ps->p++;
    } else if (c == '{') {
        if (!parse_count(ps, &min, &max))
            return atom;
    } else {
        return atom;
    }
    if (ps->p < ps->end && *ps->p == '?')
        ps->p++;
    if (min > RE_RX_MAX_REPEAT || (max != UINT_MAX && max > RE_RX_MAX_REPEAT)) {
        re_rx_fail(ps, "repetition count too large");
        return NULL;
    }
    rx_n_t *n = re_rx_node(ps, N_REP);
    if (!n)
        return NULL;
    n->a = atom;
    n->min = min;
    n->max = max;
    return n;
}

static rx_n_t *parse_cat(parse_t *ps) {
    rx_n_t *acc = NULL;
    while (ps->p < ps->end && *ps->p != '|' && *ps->p != ')') {
        rx_n_t *r = parse_rep(ps);
        if (!r)
            return NULL;
        if (!acc) {
            acc = r;
            continue;
        }
        rx_n_t *cat = re_rx_node(ps, N_CAT);
        if (!cat)
            return NULL;
        cat->a = acc;
        cat->b = r;
        acc = cat;
    }
    return acc ? acc : re_rx_node(ps, N_CAT);
}

static rx_n_t *parse_alt(parse_t *ps) {
    rx_n_t *left = parse_cat(ps);
    if (!left)
        return NULL;
    while (ps->p < ps->end && *ps->p == '|') {
        ps->p++;
        rx_n_t *right = parse_cat(ps);
        if (!right)
            return NULL;
        rx_n_t *n = re_rx_node(ps, N_ALT);
        if (!n)
            return NULL;
        n->a = left;
        n->b = right;
        left = n;
    }
    return left;
}

rx_n_t *re_rx_parse(parse_t *ps) {
    rx_n_t *root = parse_alt(ps);
    if (!root)
        return NULL;
    if (ps->p != ps->end) {
        re_rx_fail(ps, "unbalanced paren");
        return NULL;
    }
    return root;
}

size_t re_rx_emit(re_rx_t *rx, rx_op_t op) {
    if (rx->n_inst >= RE_RX_MAX_INST)
        return RX_FAIL;
    rx_i_t *i = &rx->prog[rx->n_inst];
    i->op = (uint8_t)op;
    i->ch = 0;
    i->cls = 0;
    i->x = NULL;
    i->y = NULL;
    return rx->n_inst++;
}

static size_t gen_rep(re_rx_t *rx, const rx_n_t *n, parse_t *ps) {
    size_t first = RX_NONE;
    for (unsigned i = 0; i < n->min; i++) {
        size_t a = re_rx_gen(rx, n->a, ps);
        if (a == RX_FAIL)
            return RX_FAIL;
        if (first == RX_NONE)
            first = a;
    }
    if (n->max == UINT_MAX) {
        size_t sp = re_rx_emit(rx, RXI_SPLIT);
        if (sp == RX_FAIL)
            return sp;
        size_t body = re_rx_gen(rx, n->a, ps);
        if (body == RX_FAIL)
            return body;
        size_t jmp = re_rx_emit(rx, RXI_JMP);
        if (jmp == RX_FAIL)
            return jmp;
        rx->prog[sp].x = &rx->prog[body];
        rx->prog[jmp].x = &rx->prog[sp];
        if (ps->n_exits >= RE_RX_MAX_REPEAT)
            return RX_FAIL;
        ps->exits[ps->n_exits++] = sp;
        return first == RX_NONE ? sp : first;
    }
    for (unsigned i = n->min; i < n->max; i++) {
        size_t sp = re_rx_emit(rx, RXI_SPLIT);
        if (sp == RX_FAIL)
            return sp;
        size_t body = re_rx_gen(rx, n->a, ps);
        if (body == RX_FAIL)
            return body;
        rx->prog[sp].x = &rx->prog[body];
        if (ps->n_exits >= RE_RX_MAX_REPEAT)
            return RX_FAIL;
        ps->exits[ps->n_exits++] = sp;
        if (first == RX_NONE)
            first = sp;
    }
    return first;
}

size_t re_rx_gen(re_rx_t *rx, const rx_n_t *n, parse_t *ps) {
    if (!n)
        return RX_NONE;
    switch (n->type) {
        case N_CHAR:
        case N_ANY:
        case N_CLASS:
        case N_BOL:
        case N_EOL: {
            rx_op_t op = RXI_CHAR;
            if (n->type == N_ANY)
                op = RXI_ANY;
            else if (n->type == N_CLASS)
                op = RXI_CLASS;
            else if (n->type == N_BOL)
                op = RXI_BOL;
            else if (n->type == N_EOL)
                op = RXI_EOL;
            size_t at = re_rx_emit(rx, op);
            if (at == RX_FAIL)
                return at;
            rx->prog[at].ch = n->ch;
            rx->prog[at].cls = n->cls;
            return at;
        }
        case N_CAT: {
            size_t a = re_rx_gen(rx, n->a, ps);
            if (a == RX_FAIL)
                return a;
            size_t b = re_rx_gen(rx, n->b, ps);
            if (b == RX_FAIL)
                return b;
            return a == RX_NONE ? b : a;
        }
        case N_ALT: {
            size_t sp = re_rx_emit(rx, RXI_SPLIT);
            if (sp == RX_FAIL)
                return sp;
            size_t a = re_rx_gen(rx, n->a, ps);
            if (a == RX_FAIL)
                return a;
            size_t b = re_rx_gen(rx, n->b, ps);
            if (b == RX_FAIL)
                return b;
            if (a == RX_NONE)
                a = sp;
            if (b == RX_NONE)
                b = sp;
            rx->prog[sp].x = &rx->prog[a];
            rx->prog[sp].y = &rx->prog[b];
            return sp;
        }
        case N_REP:
            return gen_rep(rx, n, ps);
    }
    return RX_FAIL;
}
