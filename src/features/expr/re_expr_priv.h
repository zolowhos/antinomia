// re_expr_priv.h - shared parser state for expression units.
// Module: feature/expr (C11).
// Owns: token/parser structs and cross-unit parse entry points.
// Depends: re_dbg.h. Not for inclusion outside features/expr.
#pragma once

#include "features/debug/re_dbg.h"

#include <stdbool.h>
#include <stdint.h>

#include "features/expr/re_expr.h"

typedef enum {
    RE_TOK_END = 0,
    RE_TOK_NUM,
    RE_TOK_IDENT,
    RE_TOK_VAR,
    RE_TOK_PLUS,
    RE_TOK_MINUS,
    RE_TOK_STAR,
    RE_TOK_SLASH,
    RE_TOK_PERCENT,
    RE_TOK_AMP,
    RE_TOK_PIPE,
    RE_TOK_CARET,
    RE_TOK_TILDE,
    RE_TOK_BANG,
    RE_TOK_LT,
    RE_TOK_GT,
    RE_TOK_LE,
    RE_TOK_GE,
    RE_TOK_EQ,
    RE_TOK_NE,
    RE_TOK_LAND,
    RE_TOK_LOR,
    RE_TOK_SHL,
    RE_TOK_SHR,
    RE_TOK_LPAREN,
    RE_TOK_RPAREN,
    RE_TOK_LBRACK,
    RE_TOK_RBRACK,
    RE_TOK_COLON
} re_tok_kind_t;

typedef struct {
    re_tok_kind_t kind;
    int64_t num;
    char text[64];
} re_tok_t;

typedef struct re_parser {
    re_dbg_session_t *s;
    const char *p;
    re_tok_t cur;
    char err[RE_EXPR_ERR_MAX];
    bool failed;
} re_parser_t;

void re_expr_fail(re_parser_t *P, const char *msg);
void re_expr_next(re_parser_t *P);
int64_t re_expr_parse_lor(re_parser_t *P);
int64_t re_expr_parse_primary(re_parser_t *P);
int64_t re_expr_parse_unary(re_parser_t *P);
