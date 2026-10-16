// main.cpp - entry point, dispatch and the error envelope. Thin by design.
// Module: cli (C++17).
// Owns: startup ordering, table driven dispatch, exit codes, error reporting.
// Depends: re_table, re_cmds, re_mcp, the utils. stdout only via the emitters.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#endif

#include "features/debug/re_dbg.h"
#include "utils/text/re_util.h"
#include "cli/app/re_shell.h"
#include "cli/app/re_table.h"
#include "cli/cmds/re_cmds.h"
#include "cli/screen/re_gui.h"
#include "mcp/re_mcp.h"
#include "mcp/re_mcp_side.h"

namespace {

constexpr const char *kName = "antinomia";
constexpr const char *kVersion = "0.1.0";
constexpr const char *kSchema = "antinomia/1";
constexpr const char *kDesc =
    "AI driven debugger for reverse engineering applications, libraries, or whatever you want";

void print_usage() {
    std::fprintf(stderr, "%s %s - %s\n\nusage:\n", kName, kVersion, kDesc);
    size_t count = 0;
    const re_cmd_t *table = re_cmd_table(&count);
    for (size_t i = 0; i < count; i++) {
        if (!table[i].fn && !re_str_eq_cstr(re_str(table[i].name), "help") &&
            !re_str_eq_cstr(re_str(table[i].name), "version") &&
            !re_str_eq_cstr(re_str(table[i].name), "mcp"))
            continue;
        std::fprintf(stderr, "  %-28s %s\n", table[i].usage, table[i].summary);
    }
    std::fprintf(stderr, "\n  global flags: --format json|text  --limit N  --offset N\n");
    std::fprintf(stderr, "  every response carries schema \"%s\"\n", kSchema);
}

int report_error(const re_err_t &err) {
    re_arena_t arena;
    re_arena_init(&arena, 4096);
    re_jw_t w;
    re_jw_init(&w, &arena);
    re_jw_obj(&w);
    re_jw_kcstr(&w, "schema", kSchema);
    re_jw_key(&w, "error");
    re_jw_obj(&w);
    re_jw_kcstr(&w, "code", re_err_json_code(err.code));
    re_jw_kstr(&w, "message", re_strn(re_err_msg(&err), re_str(re_err_msg(&err)).n));
    re_jw_ku64(&w, "exit", (uint64_t)re_err_exit_code(err.code));
    re_jw_obj_end(&w);
    re_jw_obj_end(&w);
    re_jw_flush(&w, stderr);
    re_arena_free(&arena);
    return re_err_exit_code(err.code);
}

class Utf8Console {
  public:
    explicit Utf8Console(bool on) {
#if defined(_WIN32)
        if (on && _isatty(_fileno(stdout))) {
            m_old = GetConsoleOutputCP();
            if (m_old != 65001u)
                m_changed = SetConsoleOutputCP(65001u) != 0;
        }
#else
        (void)on;
#endif
    }
    ~Utf8Console() {
#if defined(_WIN32)
        if (m_changed)
            SetConsoleOutputCP(m_old);
#endif
    }
    Utf8Console(const Utf8Console &) = delete;
    Utf8Console &operator=(const Utf8Console &) = delete;

  private:
#if defined(_WIN32)
    UINT m_old = 0;
    bool m_changed = false;
#endif
};

} // namespace

static int run_mcp(int argc, char **argv) {
    // Stdio MCP, or --attach to a TUI side-channel (ANTINOMIA_MCP / path file).
    // --side-listen hosts the side channel without a TTY (CI / demos).
    if (argc >= 3 && re_str_eq_cstr(re_str(argv[2]), "--attach"))
        return re_mcp_attach_main(argc >= 4 ? argv[3] : nullptr);
    if (argc >= 3 && re_str_eq_cstr(re_str(argv[2]), "--side-listen")) {
        char ep[256];
        if (!re_mcp_side_start(ep, sizeof(ep))) {
            std::fprintf(stderr, "mcp --side-listen could not bind a local endpoint\n");
            return 6;
        }
        std::fprintf(stderr, "%s\n", ep);
        std::fflush(stderr);
        for (;;) {
            int c = std::fgetc(stdin);
            if (c == EOF || c == 'q' || c == 'Q')
                break;
        }
        re_mcp_side_stop();
        return 0;
    }
    (void)argv;
    return re_mcp_main();
}

int run_one(const re_cmd_t *cmd, int argc, char **argv) {
    re_arena_t arena;
    re_err_t err{};
    re_ctx_t ctx{};
    re_arena_init(&arena, 256u * 1024u);
    err.code = RE_OK;
    ctx.arena = &arena;
    ctx.err = &err;
    ctx.session = re_dbg_session_global();
    int first = 0;
    if (!re_cmd_parse(&ctx, argc - 2, argv + 2, &first, &err)) {
        re_arena_free(&arena);
        return report_error(err);
    }
    const char *path = first < (argc - 2) ? argv[2 + first] : nullptr;
    if (!path && cmd->needs_path) {
        re_arena_free(&arena);
        RE_ERR_SETF(&err, RE_E_USAGE, "%s needs a file, usage: %s", cmd->name, cmd->usage);
        return report_error(err);
    }
    int code = cmd->fn(&ctx, path, argc - 2 - first, argv + 2 + first);
    if (code != 0 && ctx.err->code != RE_OK)
        report_error(*ctx.err);
    re_arena_free(&arena);
    return code;
}

int main(int argc, char **argv) {
    re_log_init_from_env();
    re_crc_init();
    bool mcp_mode = argc >= 2 && re_str_eq_cstr(re_str(argv[1]), "mcp");
    re_tui_set_stream_mode(mcp_mode);
    Utf8Console console(!mcp_mode);

    if (argc < 2) {
        if (re_shell_wanted(argc))
            return re_cmd_gui(nullptr, "", 0, argv);
        print_usage();
        return re_err_exit_code(RE_E_USAGE);
    }
    if (!re_cmd_find(argv[1]) && argc == 2 && re_gui_wants_file(argv[1])) {
        char *dropped[] = {argv[0], const_cast<char *>("gui"), argv[1]};
        return re_cmd_gui(nullptr, dropped[2], 3, dropped);
    }
    const re_cmd_t *cmd = re_cmd_find(argv[1]);
    if (!cmd) {
        re_err_t err{};
        RE_ERR_SETF(&err, RE_E_USAGE, "unknown command '%s', try %s help", argv[1], kName);
        print_usage();
        return report_error(err);
    }
    if (re_str_eq_cstr(re_str(cmd->name), "help")) {
        print_usage();
        return 0;
    }
    if (re_str_eq_cstr(re_str(cmd->name), "version")) {
        std::fprintf(stderr, "%s %s schema %s\n", kName, kVersion, kSchema);
        return 0;
    }
    if (re_str_eq_cstr(re_str(cmd->name), "mcp"))
        return run_mcp(argc, argv);
    return run_one(cmd, argc, argv);
}
