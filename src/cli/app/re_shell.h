// re_shell.h - the line oriented shell, reachable as a command.
// Module: cli (C11).
// Owns: the prompt loop, and remembering the file being looked at.
// Depends: re_table, re_report, re_tui, re_cmds. Dispatches through the same command
//           table the argv path uses, so the shell is not a second implementation.
//
// The rule that shapes this: with no arguments and a terminal on both ends, stay put
// and read commands. That is what makes double clicking the binary useful. With no
// arguments and a pipe, print usage and exit, because something automated is asking
// and it must not be made to wait for a keystroke that will never come.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "cli/app/re_table.h"

// True when a terminal is on both ends. The shell reads keystrokes and writes frames,
// and it must refuse rather than sit there waiting inside a pipe.
bool re_shell_wanted(int argc);

// The shell as a command, so it stays reachable now that a bare invocation opens the
// view. Reports a terminal-less environment rather than waiting in one.
int re_cmd_shell(re_ctx_t *ctx, const char *path, int argc, char **argv);

// Run the loop. Returns 0 on a clean exit, which is the only way it returns: an error
// inside a command is reported and the loop continues, because losing a whole session
// to one bad argument is not a useful behaviour for a thing you are exploring with.
int re_shell_run(re_ctx_t *ctx);
#ifdef __cplusplus
}
#endif
