// re_shell.c - the interactive mode entered when the tool is run with no arguments.
// Module: cli (C11).
// Owns: the prompt loop, line splitting, and dispatch through the command table.
// Depends: re_shell.h, re_table, re_tui, re_cmds. No analysis session cache.
#include "cli/app/re_shell.h"

#include "features/debug/re_dbg.h"
#include "utils/text/re_str.h"
#include "utils/tui/re_tui.h"
#include "cli/cmds/re_cmds.h"

#include <stdio.h>
#include <string.h>

#define SHELL_LINE_MAX 512
#define SHELL_ARGS_MAX 32

typedef struct {
    char *argv[SHELL_ARGS_MAX];
    int argc;
    re_arena_t *a;
} line_t;

static size_t text_len(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

bool re_shell_wanted(int argc) {
    if (argc >= 2)
        return false;
    return re_tui_is_tty() && re_tui_stdin_tty();
}

static bool split(const char *line, line_t *out, re_arena_t *a) {
    out->argc = 0;
    out->a = a;
    while (*line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line)
            break;
        char quote = 0;
        if (*line == '"' || *line == '\'')
            quote = *line++;
        const char *start = line;
        while (*line && (quote ? *line != quote : (*line != ' ' && *line != '\t')))
            line++;
        size_t n = (size_t)(line - start);
        if (out->argc >= SHELL_ARGS_MAX)
            return false;
        char *word = (char *)re_arena_alloc(a, n + 1);
        if (!word)
            return false;
        memcpy(word, start, n);
        word[n] = 0;
        out->argv[out->argc++] = word;
        if (quote && *line == quote)
            line++;
    }
    return true;
}

int re_shell_run(re_ctx_t *ctx) {
    char line[SHELL_LINE_MAX];
    fprintf(stderr, "antinomia> ");
    fflush(stderr);
    while (fgets(line, (int)sizeof(line), stdin)) {
        size_t n = text_len(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (n == 0) {
            fprintf(stderr, "antinomia> ");
            fflush(stderr);
            continue;
        }
        if (re_str_eq_cstr(re_str(line), "quit") || re_str_eq_cstr(re_str(line), "exit") ||
            re_str_eq_cstr(re_str(line), "q"))
            return 0;
        re_arena_t a;
        re_arena_init(&a, 64u * 1024u);
        line_t words;
        if (!split(line, &words, &a) || words.argc < 1) {
            re_arena_free(&a);
            fprintf(stderr, "antinomia> ");
            fflush(stderr);
            continue;
        }
        if (re_str_eq_cstr(re_str(words.argv[0]), "help")) {
            size_t count = 0;
            const re_cmd_t *table = re_cmd_table(&count);
            for (size_t i = 0; i < count; i++) {
                if (!table[i].fn)
                    continue;
                fprintf(stderr, "  %-28s %s\n", table[i].usage, table[i].summary);
            }
            re_arena_free(&a);
            fprintf(stderr, "antinomia> ");
            fflush(stderr);
            continue;
        }
        const re_cmd_t *cmd = re_cmd_find(words.argv[0]);
        if (!cmd || !cmd->fn) {
            fprintf(stderr, "unknown command '%s'\n", words.argv[0]);
            re_arena_free(&a);
            fprintf(stderr, "antinomia> ");
            fflush(stderr);
            continue;
        }
        re_err_t err;
        err.code = RE_OK;
        re_ctx_t local = *ctx;
        local.arena = &a;
        local.err = &err;
        local.session = re_dbg_session_global();
        int first = 0;
        if (!re_cmd_parse(&local, words.argc - 1, words.argv + 1, &first, &err)) {
            fprintf(stderr, "%s\n", re_err_msg(&err));
        } else {
            const char *path = first < (words.argc - 1) ? words.argv[1 + first] : NULL;
            if (!path && cmd->needs_path)
                fprintf(stderr, "%s needs a file\n", cmd->name);
            else
                (void)cmd->fn(&local, path, words.argc - 1 - first, words.argv + 1 + first);
        }
        re_arena_free(&a);
        fprintf(stderr, "antinomia> ");
        fflush(stderr);
    }
    return 0;
}

int re_cmd_shell(re_ctx_t *ctx, const char *path, int argc, char **argv) {
    (void)path;
    (void)argc;
    (void)argv;
    if (!re_tui_is_tty() || !re_tui_stdin_tty()) {
        if (ctx->err)
            RE_ERR_SET(ctx->err, RE_E_USAGE, "shell needs a terminal on stdin and stdout");
        return 2;
    }
    return re_shell_run(ctx);
}
