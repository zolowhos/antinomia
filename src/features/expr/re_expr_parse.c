// re_expr_parse.c - binary operator precedence and eval entry points.
// Module: feature/expr (C11).
// Owns: mul/add/shift/cmp/bit/logic chains and re_expr_eval.
// Depends: re_expr_priv.h.
#include "features/expr/re_expr_priv.h"

#include <string.h>

static int64_t parse_mul(re_parser_t *P) {
    int64_t left = re_expr_parse_unary(P);
    while (!P->failed && (P->cur.kind == RE_TOK_STAR || P->cur.kind == RE_TOK_SLASH ||
                          P->cur.kind == RE_TOK_PERCENT)) {
        re_tok_kind_t op = P->cur.kind;
        int64_t right;
        re_expr_next(P);
        right = re_expr_parse_unary(P);
        if (op == RE_TOK_STAR)
            left = left * right;
        else if (right == 0) {
            re_expr_fail(P, "division by zero");
            return 0;
        } else if (op == RE_TOK_SLASH)
            left = left / right;
        else
            left = left % right;
    }
    return left;
}

static int64_t parse_add(re_parser_t *P) {
    int64_t left = parse_mul(P);
    while (!P->failed && (P->cur.kind == RE_TOK_PLUS || P->cur.kind == RE_TOK_MINUS)) {
        re_tok_kind_t op = P->cur.kind;
        int64_t right;
        re_expr_next(P);
        right = parse_mul(P);
        left = op == RE_TOK_PLUS ? left + right : left - right;
    }
    return left;
}

static int64_t parse_shift(re_parser_t *P) {
    int64_t left = parse_add(P);
    while (!P->failed && (P->cur.kind == RE_TOK_SHL || P->cur.kind == RE_TOK_SHR)) {
        re_tok_kind_t op = P->cur.kind;
        int64_t right;
        re_expr_next(P);
        right = parse_add(P);
        if (right < 0 || right > 63) {
            re_expr_fail(P, "shift out of range");
            return 0;
        }
        left = op == RE_TOK_SHL ? (int64_t)((uint64_t)left << (unsigned)right)
                                : (int64_t)((uint64_t)left >> (unsigned)right);
    }
    return left;
}

static int64_t parse_rel(re_parser_t *P) {
    int64_t left = parse_shift(P);
    while (!P->failed && (P->cur.kind == RE_TOK_LT || P->cur.kind == RE_TOK_GT ||
                          P->cur.kind == RE_TOK_LE || P->cur.kind == RE_TOK_GE)) {
        re_tok_kind_t op = P->cur.kind;
        int64_t right;
        re_expr_next(P);
        right = parse_shift(P);
        if (op == RE_TOK_LT)
            left = left < right;
        else if (op == RE_TOK_GT)
            left = left > right;
        else if (op == RE_TOK_LE)
            left = left <= right;
        else
            left = left >= right;
    }
    return left;
}

static int64_t parse_eq(re_parser_t *P) {
    int64_t left = parse_rel(P);
    while (!P->failed && (P->cur.kind == RE_TOK_EQ || P->cur.kind == RE_TOK_NE)) {
        re_tok_kind_t op = P->cur.kind;
        int64_t right;
        re_expr_next(P);
        right = parse_rel(P);
        left = op == RE_TOK_EQ ? left == right : left != right;
    }
    return left;
}

static int64_t parse_bitand(re_parser_t *P) {
    int64_t left = parse_eq(P);
    while (!P->failed && P->cur.kind == RE_TOK_AMP) {
        int64_t right;
        re_expr_next(P);
        right = parse_eq(P);
        left = left & right;
    }
    return left;
}

static int64_t parse_bitxor(re_parser_t *P) {
    int64_t left = parse_bitand(P);
    while (!P->failed && P->cur.kind == RE_TOK_CARET) {
        int64_t right;
        re_expr_next(P);
        right = parse_bitand(P);
        left = left ^ right;
    }
    return left;
}

static int64_t parse_bitor(re_parser_t *P) {
    int64_t left = parse_bitxor(P);
    while (!P->failed && P->cur.kind == RE_TOK_PIPE) {
        int64_t right;
        re_expr_next(P);
        right = parse_bitxor(P);
        left = left | right;
    }
    return left;
}

static int64_t parse_land(re_parser_t *P) {
    int64_t left = parse_bitor(P);
    while (!P->failed && P->cur.kind == RE_TOK_LAND) {
        int64_t right;
        re_expr_next(P);
        right = parse_bitor(P);
        left = (left != 0 && right != 0) ? 1 : 0;
    }
    return left;
}

int64_t re_expr_parse_lor(re_parser_t *P) {
    int64_t left = parse_land(P);
    while (!P->failed && P->cur.kind == RE_TOK_LOR) {
        int64_t right;
        re_expr_next(P);
        right = parse_land(P);
        left = (left != 0 || right != 0) ? 1 : 0;
    }
    return left;
}

static void copy_err(re_parser_t *P, char *err, size_t err_cap) {
    size_t i = 0;
    if (!err || err_cap == 0)
        return;
    while (P->err[i] && i + 1u < err_cap) {
        err[i] = P->err[i];
        i++;
    }
    err[i] = 0;
}

static re_err_code_t eval_inner(re_dbg_session_t *s, const char *src, int64_t *out, char *err,
                                size_t err_cap) {
    re_parser_t P;
    int64_t v;
    if (!s || !src || !out)
        return RE_E_USAGE;
    memset(&P, 0, sizeof(P));
    P.s = s;
    P.p = src;
    re_expr_next(&P);
    if (P.failed) {
        copy_err(&P, err, err_cap);
        return RE_E_MALFORMED;
    }
    v = re_expr_parse_lor(&P);
    if (P.failed) {
        copy_err(&P, err, err_cap);
        return RE_E_MALFORMED;
    }
    if (P.cur.kind != RE_TOK_END) {
        re_expr_fail(&P, "trailing tokens in expression");
        copy_err(&P, err, err_cap);
        return RE_E_MALFORMED;
    }
    *out = v;
    if (err && err_cap)
        err[0] = 0;
    return RE_OK;
}

re_err_code_t re_expr_eval(re_dbg_session_t *s, const char *src, int64_t *out, char *err,
                           size_t err_cap) {
    return eval_inner(s, src, out, err, err_cap);
}

re_err_code_t re_expr_eval_u64(re_dbg_session_t *s, const char *src, uint64_t *out, char *err,
                               size_t err_cap) {
    int64_t v = 0;
    re_err_code_t c = eval_inner(s, src, &v, err, err_cap);
    if (c != RE_OK)
        return c;
    if (out)
        *out = (uint64_t)v;
    return RE_OK;
}
