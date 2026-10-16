// re_expr.c - lexer and primary/unary parsers for debugger expressions.
// Module: feature/expr (C11).
// Owns: tokenization and atom parsing (regs, $vars, mem, module:symbol).
// Depends: re_expr_priv.h, session regs/mem/resolve/var APIs.
#include "features/expr/re_expr_priv.h"

#include "utils/text/re_str.h"

#include <string.h>

void re_expr_fail(re_parser_t *P, const char *msg) {
    size_t i = 0;
    if (P->failed)
        return;
    P->failed = true;
    if (!msg)
        msg = "expression error";
    while (msg[i] && i + 1u < sizeof(P->err)) {
        P->err[i] = msg[i];
        i++;
    }
    P->err[i] = 0;
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void skip_ws(re_parser_t *P) {
    while (*P->p && is_space(*P->p))
        P->p++;
}

static bool parse_num(re_parser_t *P, re_tok_t *t) {
    const char *s = P->p;
    uint64_t base = 10;
    uint64_t acc = 0;
    size_t i = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (!*s)
        return false;
    if (!(s[0] >= '0' && s[0] <= '9') &&
        !(base == 16 && ((s[0] >= 'a' && s[0] <= 'f') || (s[0] >= 'A' && s[0] <= 'F'))))
        return false;
    while (*s) {
        char c = *s;
        uint64_t d;
        if (c >= '0' && c <= '9')
            d = (uint64_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint64_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint64_t)(c - 'A' + 10);
        else
            break;
        if (d >= base)
            break;
        acc = acc * base + d;
        s++;
        i++;
    }
    if (i == 0)
        return false;
    P->p = s;
    t->kind = RE_TOK_NUM;
    t->num = (int64_t)acc;
    t->text[0] = 0;
    return true;
}

static bool parse_ident(re_parser_t *P, re_tok_t *t) {
    size_t n = 0;
    if (!is_ident_start(*P->p))
        return false;
    while (is_ident(*P->p) && n + 1u < sizeof(t->text))
        t->text[n++] = *P->p++;
    t->text[n] = 0;
    t->kind = RE_TOK_IDENT;
    t->num = 0;
    return true;
}

static bool lex_two(re_parser_t *P, char a, char b, re_tok_kind_t kind) {
    if (P->p[0] != a || P->p[1] != b)
        return false;
    P->p += 2;
    P->cur.kind = kind;
    return true;
}

static re_tok_kind_t lex_single_kind(char c) {
    switch (c) {
        case '+':
            return RE_TOK_PLUS;
        case '-':
            return RE_TOK_MINUS;
        case '*':
            return RE_TOK_STAR;
        case '/':
            return RE_TOK_SLASH;
        case '%':
            return RE_TOK_PERCENT;
        case '&':
            return RE_TOK_AMP;
        case '|':
            return RE_TOK_PIPE;
        case '^':
            return RE_TOK_CARET;
        case '~':
            return RE_TOK_TILDE;
        case '!':
            return RE_TOK_BANG;
        case '<':
            return RE_TOK_LT;
        case '>':
            return RE_TOK_GT;
        case '(':
            return RE_TOK_LPAREN;
        case ')':
            return RE_TOK_RPAREN;
        case '[':
            return RE_TOK_LBRACK;
        case ']':
            return RE_TOK_RBRACK;
        case ':':
            return RE_TOK_COLON;
        default:
            return RE_TOK_END;
    }
}

static bool lex_compound(re_parser_t *P) {
    return lex_two(P, '<', '<', RE_TOK_SHL) || lex_two(P, '>', '>', RE_TOK_SHR) ||
           lex_two(P, '&', '&', RE_TOK_LAND) || lex_two(P, '|', '|', RE_TOK_LOR) ||
           lex_two(P, '=', '=', RE_TOK_EQ) || lex_two(P, '!', '=', RE_TOK_NE) ||
           lex_two(P, '<', '=', RE_TOK_LE) || lex_two(P, '>', '=', RE_TOK_GE);
}

void re_expr_next(re_parser_t *P) {
    re_tok_t *t = &P->cur;
    const char *p;
    re_tok_kind_t k;
    memset(t, 0, sizeof(*t));
    skip_ws(P);
    p = P->p;
    if (!*p) {
        t->kind = RE_TOK_END;
        return;
    }
    if (*p == '$') {
        P->p++;
        if (!parse_ident(P, t)) {
            re_expr_fail(P, "expected variable name after $");
            t->kind = RE_TOK_END;
            return;
        }
        t->kind = RE_TOK_VAR;
        return;
    }
    if (parse_num(P, t) || parse_ident(P, t) || lex_compound(P))
        return;
    P->p++;
    k = lex_single_kind(p[0]);
    if (k == RE_TOK_END) {
        re_expr_fail(P, "unexpected character in expression");
        t->kind = RE_TOK_END;
        return;
    }
    t->kind = k;
}

static bool reg_value(re_dbg_session_t *s, const char *name, uint64_t *out) {
    re_dbg_regs_t r;
    re_str_t n = re_str(name);
    if (re_dbg_session_get_regs(s, 0, &r) != RE_OK)
        return false;
    if (re_str_ieq_cstr(n, "rax"))
        *out = r.rax;
    else if (re_str_ieq_cstr(n, "rbx"))
        *out = r.rbx;
    else if (re_str_ieq_cstr(n, "rcx"))
        *out = r.rcx;
    else if (re_str_ieq_cstr(n, "rdx"))
        *out = r.rdx;
    else if (re_str_ieq_cstr(n, "rsi"))
        *out = r.rsi;
    else if (re_str_ieq_cstr(n, "rdi"))
        *out = r.rdi;
    else if (re_str_ieq_cstr(n, "rbp"))
        *out = r.rbp;
    else if (re_str_ieq_cstr(n, "rsp"))
        *out = r.rsp;
    else if (re_str_ieq_cstr(n, "r8"))
        *out = r.r8;
    else if (re_str_ieq_cstr(n, "r9"))
        *out = r.r9;
    else if (re_str_ieq_cstr(n, "r10"))
        *out = r.r10;
    else if (re_str_ieq_cstr(n, "r11"))
        *out = r.r11;
    else if (re_str_ieq_cstr(n, "r12"))
        *out = r.r12;
    else if (re_str_ieq_cstr(n, "r13"))
        *out = r.r13;
    else if (re_str_ieq_cstr(n, "r14"))
        *out = r.r14;
    else if (re_str_ieq_cstr(n, "r15"))
        *out = r.r15;
    else if (re_str_ieq_cstr(n, "rip"))
        *out = r.rip;
    else if (re_str_ieq_cstr(n, "rflags") || re_str_ieq_cstr(n, "eflags"))
        *out = r.rflags;
    else
        return false;
    return true;
}

static size_t type_width(const char *name) {
    re_str_t n = re_str(name);
    if (re_str_ieq_cstr(n, "byte") || re_str_ieq_cstr(n, "uint8") || re_str_ieq_cstr(n, "u8"))
        return 1;
    if (re_str_ieq_cstr(n, "word") || re_str_ieq_cstr(n, "uint16") || re_str_ieq_cstr(n, "u16"))
        return 2;
    if (re_str_ieq_cstr(n, "dword") || re_str_ieq_cstr(n, "uint32") || re_str_ieq_cstr(n, "u32"))
        return 4;
    if (re_str_ieq_cstr(n, "qword") || re_str_ieq_cstr(n, "uint64") || re_str_ieq_cstr(n, "u64") ||
        re_str_ieq_cstr(n, "ptr"))
        return 8;
    return 0;
}

static int64_t read_mem_val(re_parser_t *P, uint64_t addr, size_t width) {
    uint8_t buf[8];
    uint64_t v = 0;
    size_t i;
    if (width == 0 || width > 8) {
        re_expr_fail(P, "invalid memory width");
        return 0;
    }
    memset(buf, 0, sizeof(buf));
    if (re_dbg_session_read_mem(P->s, addr, buf, width) != RE_OK) {
        re_expr_fail(P, "memory read failed");
        return 0;
    }
    for (i = 0; i < width; i++)
        v |= (uint64_t)buf[i] << (8u * (unsigned)i);
    return (int64_t)v;
}

static int64_t parse_mem(re_parser_t *P, size_t width) {
    int64_t addr;
    if (P->cur.kind != RE_TOK_LBRACK) {
        re_expr_fail(P, "expected [ after type:");
        return 0;
    }
    re_expr_next(P);
    addr = re_expr_parse_lor(P);
    if (P->failed)
        return 0;
    if (P->cur.kind != RE_TOK_RBRACK) {
        re_expr_fail(P, "expected ] after memory expression");
        return 0;
    }
    re_expr_next(P);
    return read_mem_val(P, (uint64_t)addr, width);
}

static int64_t parse_module_sym(re_parser_t *P, const char *mod) {
    char query[128];
    uint64_t addr = 0;
    size_t n = 0;
    size_t j = 0;
    if (P->cur.kind != RE_TOK_IDENT) {
        re_expr_fail(P, "expected symbol after module:");
        return 0;
    }
    while (mod[n] && n + 1u < sizeof(query)) {
        query[n] = mod[n];
        n++;
    }
    query[n++] = ':';
    while (P->cur.text[j] && n + 1u < sizeof(query))
        query[n++] = P->cur.text[j++];
    query[n] = 0;
    re_expr_next(P);
    if (re_dbg_session_resolve(P->s, query, &addr, NULL) != RE_OK) {
        re_expr_fail(P, "module:symbol not found");
        return 0;
    }
    return (int64_t)addr;
}

int64_t re_expr_parse_primary(re_parser_t *P) {
    int64_t v = 0;
    if (P->failed)
        return 0;
    if (P->cur.kind == RE_TOK_NUM) {
        v = P->cur.num;
        re_expr_next(P);
        return v;
    }
    if (P->cur.kind == RE_TOK_VAR) {
        if (re_dbg_session_var_get(P->s, P->cur.text, &v) != RE_OK) {
            re_expr_fail(P, "unknown $variable");
            return 0;
        }
        re_expr_next(P);
        return v;
    }
    if (P->cur.kind == RE_TOK_LBRACK)
        return parse_mem(P, 8);
    if (P->cur.kind == RE_TOK_LPAREN) {
        re_expr_next(P);
        v = re_expr_parse_lor(P);
        if (P->cur.kind != RE_TOK_RPAREN) {
            re_expr_fail(P, "expected )");
            return 0;
        }
        re_expr_next(P);
        return v;
    }
    if (P->cur.kind == RE_TOK_IDENT) {
        char name[64];
        size_t w;
        uint64_t rv = 0;
        memcpy(name, P->cur.text, sizeof(name));
        re_expr_next(P);
        if (P->cur.kind == RE_TOK_COLON) {
            re_expr_next(P);
            w = type_width(name);
            if (w != 0)
                return parse_mem(P, w);
            return parse_module_sym(P, name);
        }
        if (reg_value(P->s, name, &rv))
            return (int64_t)rv;
        re_expr_fail(P, "unknown identifier");
        return 0;
    }
    re_expr_fail(P, "expected expression");
    return 0;
}

int64_t re_expr_parse_unary(re_parser_t *P) {
    if (P->cur.kind == RE_TOK_MINUS) {
        re_expr_next(P);
        return -re_expr_parse_unary(P);
    }
    if (P->cur.kind == RE_TOK_PLUS) {
        re_expr_next(P);
        return re_expr_parse_unary(P);
    }
    if (P->cur.kind == RE_TOK_BANG) {
        re_expr_next(P);
        return re_expr_parse_unary(P) == 0 ? 1 : 0;
    }
    if (P->cur.kind == RE_TOK_TILDE) {
        re_expr_next(P);
        return ~re_expr_parse_unary(P);
    }
    return re_expr_parse_primary(P);
}
