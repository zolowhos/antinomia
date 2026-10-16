// re_table.c - the command table and the shared flag parser.
// Module: cli (C11).
// Owns: command metadata and shared flag parsing for every command.
// Depends: re_table.h and the command entry points. No I/O beyond stdout.
#include "cli/app/re_table.h"

#include "utils/text/re_hex.h"
#include "utils/text/re_str.h"
#include "cli/app/re_shell.h"
#include "cli/cmds/re_cmds.h"
#include "cli/screen/re_gui.h"

#include <stdio.h>
#include <string.h>

static const re_cmd_t kCommands[] = {
    {"gui", "empty debugger view in the terminal", "gui [file]", re_cmd_gui, false, true, false},
    {"shell", "line oriented prompt for debug commands", "shell", re_cmd_shell, false, true, false},
    {"status", "session status: pid, tid, last event", "status", re_cmd_status, false, false, true},
    {"init", "launch a target under the debugger", "init <file>", re_cmd_init, true, false, true},
    {"launch", "alias for init", "launch <file>", re_cmd_launch, true, false, true},
    {"attach", "attach to a running process by pid", "attach <pid>", re_cmd_attach, false, false,
     true},
    {"detach", "detach without killing the target", "detach", re_cmd_detach, false, false, true},
    {"kill", "stop debug and kill the target", "kill", re_cmd_kill, false, false, true},
    {"wait_event", "wait for the next debug event", "wait_event", re_cmd_wait_event, false, false,
     true},
    {"run", "continue execution", "run", re_cmd_run, false, false, true},
    {"g", "alias for run", "g", re_cmd_run, false, false, true},
    {"cont", "alias for run", "cont", re_cmd_cont, false, false, true},
    {"step", "step into (single step)", "step", re_cmd_step, false, false, true},
    {"sti", "step into", "sti", re_cmd_step_into, false, false, true},
    {"t", "alias for sti", "t", re_cmd_step_into, false, false, true},
    {"sto", "step over (skip call)", "sto", re_cmd_step_over, false, false, true},
    {"p", "alias for sto", "p", re_cmd_step_over, false, false, true},
    {"pause", "interrupt the target", "pause", re_cmd_pause, false, false, true},
    {"bp", "set software breakpoint", "bp <addr|expr>", re_cmd_bp, false, false, true},
    {"bc", "clear breakpoint", "bc <addr|expr>", re_cmd_bc, false, false, true},
    {"be", "enable breakpoint", "be <addr|expr>", re_cmd_be, false, false, true},
    {"bd", "disable breakpoint", "bd <addr|expr>", re_cmd_bd, false, false, true},
    {"bplist", "list breakpoints", "bplist", re_cmd_bplist, false, false, true},
    {"bph", "set hardware breakpoint/watch", "bph <addr> [size] [x|w|rw]", re_cmd_bph, false, false,
     true},
    {"bphw", "alias for bph", "bphw <addr> [size] [x|w|rw]", re_cmd_bph, false, false, true},
    {"bphc", "clear hardware breakpoint", "bphc <addr>", re_cmd_bph_clear, false, false, true},
    {"bpm", "set memory watch (DR-backed)", "bpm <addr> [len] [w|rw]", re_cmd_bpm, false, false,
     true},
    {"bpmc", "clear memory watch", "bpmc <addr>", re_cmd_bpm_clear, false, false, true},
    {"bpdll", "break on module load (name substring)", "bpdll <name>", re_cmd_bpdll, false, false,
     true},
    {"bpdllc", "clear module-load breakpoint", "bpdllc <name>", re_cmd_bpdll_clear, false, false,
     true},
    {"bpc", "set BP break condition", "bpc <addr> <expr>", re_cmd_bpc, false, false, true},
    {"bplog", "set BP log text", "bplog <addr> <text>", re_cmd_bplog, false, false, true},
    {"bplogc", "set BP log condition", "bplogc <addr> <expr>", re_cmd_bplogc, false, false, true},
    {"bpcmd", "set BP command", "bpcmd <addr> <cmd>", re_cmd_bpcmd, false, false, true},
    {"bpcmdc", "set BP command condition", "bpcmdc <addr> <expr>", re_cmd_bpcmdc, false, false,
     true},
    {"bpfr", "set BP fast resume", "bpfr <addr> <0|1>", re_cmd_bpfr, false, false, true},
    {"expr", "evaluate C-like expression", "expr <expression>", re_cmd_expr, false, false, true},
    {"?", "alias for expr", "? <expression>", re_cmd_expr, false, false, true},
    {"var", "get/set $variable", "var <name> [value]", re_cmd_var, false, false, true},
    {"watch", "add watch expression", "watch <expression>", re_cmd_watch, false, false, true},
    {"wa", "alias for watch", "wa <expression>", re_cmd_watch, false, false, true},
    {"watchc", "clear watch by index or *", "watchc <index|*>", re_cmd_watchc, false, false, true},
    {"watchlist", "list and evaluate watches", "watchlist", re_cmd_watchlist, false, false, true},
    {"regs", "read general purpose registers", "regs", re_cmd_regs, false, false, true},
    {"set_regs", "write GPRs as name=value", "set_regs name=val...", re_cmd_set_regs, false, false,
     true},
    {"d", "hex dump memory at addr or RIP", "d [addr] [--len N]", re_cmd_dump, false, false, true},
    {"u", "disassemble at addr or RIP", "u [addr] [--limit N]", re_cmd_disasm, false, false, true},
    {"graph", "function-local CFG (basic blocks)", "graph [addr]", re_cmd_graph, false, false,
     true},
    {"cfg", "alias for graph", "cfg [addr]", re_cmd_graph, false, false, true},
    {"find", "search memory for pattern/value", "find <pattern> [--off A] [--len N]",
     re_cmd_mem_find, false, false, true},
    {"search", "alias for find", "search <pattern> [--off A] [--len N]", re_cmd_search, false,
     false, true},
    {"mem_read", "read target memory", "mem_read <addr> [--len N]", re_cmd_mem_read, false, false,
     true},
    {"mem_write", "write hex bytes to memory", "mem_write <addr> <hex>", re_cmd_mem_write, false,
     false, true},
    {"threads", "list threads", "threads", re_cmd_threads, false, false, true},
    {"modules", "list loaded modules", "modules", re_cmd_modules, false, false, true},
    {"lm", "alias for modules", "lm", re_cmd_modules, false, false, true},
    {"sym", "resolve module:symbol (PE export / ELF dynsym)", "sym <module:symbol>", re_cmd_sym,
     false, false, true},
    {"bt", "stack walk v0 (frame pointer or rsp dump)", "bt", re_cmd_bt, false, false, true},
    {"exbp", "break on exception/signal code", "exbp <code> [--handle]", re_cmd_exbp, false, false,
     true},
    {"exignore", "ignore/swallow exception code range", "exignore <lo> [hi]", re_cmd_exignore,
     false, false, true},
    {"expass", "pass exception/signal to inferior", "expass <lo> [hi]", re_cmd_expass, false, false,
     true},
    {"exclear", "clear exception policy rule", "exclear <lo> [hi]", re_cmd_exclear, false, false,
     true},
    {"exlist", "list exception/signal policies", "exlist", re_cmd_exlist, false, false, true},
    {"caps", "list backend capability bits", "caps", re_cmd_caps, false, false, true},
    {"help", "print usage", "help", NULL, false, false, false},
    {"version", "print version and schema", "version", NULL, false, false, false},
    {"mcp", "run the MCP server over stdio", "mcp", NULL, false, false, false},
};

const re_cmd_t *re_cmd_table(size_t *count) {
    if (count)
        *count = sizeof(kCommands) / sizeof(kCommands[0]);
    return kCommands;
}

const re_cmd_t *re_cmd_find(const char *name) {
    size_t n = 0;
    const re_cmd_t *t = re_cmd_table(&n);
    for (size_t i = 0; i < n; i++) {
        if (re_str_eq_cstr(re_str(t[i].name), name))
            return &t[i];
    }
    return NULL;
}

void *re_ctx_out(const re_ctx_t *ctx) {
    return ctx && ctx->stream ? ctx->stream : stdout;
}

void re_cmd_begin(re_ctx_t *ctx, re_strbuf_t *head) {
    (void)ctx;
    re_strbuf_puts(head, "{\"schema\":\"antinomia/1\"");
}

static bool parse_u64(const char *s, uint64_t *out) {
    re_str_t v = re_str(s);
    if (v.n == 0)
        return false;
    uint64_t base = 10;
    size_t i = 0;
    if (v.n > 2 && v.p[0] == '0' && (v.p[1] == 'x' || v.p[1] == 'X')) {
        base = 16;
        i = 2;
    }
    uint64_t acc = 0;
    for (; i < v.n; i++) {
        int d = re_hex_val(v.p[i]);
        if (d < 0 || (uint64_t)d >= base)
            return false;
        acc = acc * base + (uint64_t)d;
    }
    *out = acc;
    return true;
}

static bool takes_value(re_str_t a) {
    return re_str_eq_cstr(a, "--format") || re_str_eq_cstr(a, "--limit") ||
           re_str_eq_cstr(a, "--offset") || re_str_eq_cstr(a, "--off") ||
           re_str_eq_cstr(a, "--len") || re_str_eq_cstr(a, "--rva");
}

static const char *glued_value(re_str_t a) {
    if (re_str_starts_cstr(a, "--format="))
        return a.p + 9;
    if (re_str_starts_cstr(a, "--limit="))
        return a.p + 8;
    if (re_str_starts_cstr(a, "--offset="))
        return a.p + 9;
    if (re_str_starts_cstr(a, "--len="))
        return a.p + 6;
    if (re_str_starts_cstr(a, "--off="))
        return a.p + 6;
    if (re_str_starts_cstr(a, "--rva="))
        return a.p + 6;
    return NULL;
}

static bool apply_flag(re_ctx_t *ctx, re_str_t a, const char *val, re_err_t *err) {
    if (re_str_starts_cstr(a, "--format")) {
        re_str_t v = re_str(val);
        if (re_str_eq_cstr(v, "text"))
            ctx->out = RE_FMT_OUT_TEXT;
        else if (re_str_eq_cstr(v, "json"))
            ctx->out = RE_FMT_OUT_JSON;
        else {
            RE_ERR_SETF(err, RE_E_USAGE, "--format must be json or text, got %s", val);
            return false;
        }
        return true;
    }
    uint64_t n = 0;
    if (!parse_u64(val, &n)) {
        RE_ERR_SETF(err, RE_E_USAGE, "expected a number, got %s", val);
        return false;
    }
    if (re_str_starts_cstr(a, "--limit"))
        ctx->limit = (size_t)n;
    else if (re_str_starts_cstr(a, "--offset"))
        ctx->offset = (size_t)n;
    else if (re_str_starts_cstr(a, "--len"))
        ctx->len = n;
    else if (re_str_starts_cstr(a, "--off") || re_str_starts_cstr(a, "--rva"))
        ctx->off = n;
    else
        return false;
    return true;
}

bool re_cmd_parse(re_ctx_t *ctx, int argc, char **argv, int *first_positional, re_err_t *err) {
    ctx->out = RE_FMT_OUT_JSON;
    ctx->limit = 200;
    ctx->offset = 0;
    ctx->off = 0;
    ctx->len = 256;
    ctx->no_color = false;
    int pos = argc;
    for (int i = 0; i < argc; i++) {
        re_str_t a = re_str(argv[i]);
        const char *val;
        if (!re_str_starts_cstr(a, "--")) {
            if (pos == argc)
                pos = i;
            continue;
        }
        val = glued_value(a);
        if (!val && takes_value(a)) {
            if (i + 1 >= argc) {
                RE_ERR_SETF(err, RE_E_USAGE, "flag %s needs a value", argv[i]);
                return false;
            }
            val = argv[++i];
        } else if (!val && re_str_eq_cstr(a, "--no-color")) {
            ctx->no_color = true;
            continue;
        } else if (!val) {
            RE_ERR_SETF(err, RE_E_USAGE, "unknown flag %s", argv[i]);
            return false;
        }
        if (!apply_flag(ctx, a, val, err))
            return false;
    }
    *first_positional = pos;
    return true;
}

const char *re_cmd_positional(int argc, char **argv, int from) {
    int i = from;
    while (i < argc) {
        re_str_t a = re_str(argv[i]);
        if (re_str_starts_cstr(a, "--")) {
            i += 2;
            continue;
        }
        return argv[i];
    }
    return NULL;
}
