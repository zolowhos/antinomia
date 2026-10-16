// re_ai_exsum.c - summarize Win exception codes or Linux signals for agents.
// Module: feature/ai (C11).
// Owns: name/summary/advice strings for summarize_ex.
// Depends: re_ai.h. Auto-detects signal vs NTSTATUS-ish codes; no cloud.
#include "features/ai/re_ai.h"

#include <stdio.h>
#include <string.h>

static void copy_cap(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static bool is_linux_signo(int32_t code) {
    return code > 0 && code < 64;
}

static void fill_linux(int32_t code, re_ai_exsum_t *out) {
    static const char *const names[] = {
        NULL,        "SIGHUP",  "SIGINT",    "SIGQUIT", "SIGILL",   "SIGTRAP", "SIGABRT", "SIGBUS",
        "SIGFPE",    "SIGKILL", "SIGUSR1",   "SIGSEGV", "SIGUSR2",  "SIGPIPE", "SIGALRM", "SIGTERM",
        "SIGSTKFLT", "SIGCHLD", "SIGCONT",   "SIGSTOP", "SIGTSTP",  "SIGTTIN", "SIGTTOU", "SIGURG",
        "SIGXCPU",   "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",   "SIGPWR",  "SIGSYS"};
    const char *nm = NULL;
    if (code > 0 && (size_t)code < sizeof(names) / sizeof(names[0]))
        nm = names[code];
    if (nm)
        copy_cap(out->name, sizeof(out->name), nm);
    else
        snprintf(out->name, sizeof(out->name), "SIG%d", (int)code);
    switch (code) {
        case 11: /* SIGSEGV */
            copy_cap(out->summary, sizeof(out->summary),
                     "Segmentation fault: invalid memory reference.");
            copy_cap(out->advice, sizeof(out->advice),
                     "Inspect fault addr, maps, and recent writes; consider bpm on the page.");
            break;
        case 4: /* SIGILL */
            copy_cap(out->summary, sizeof(out->summary), "Illegal instruction at the fault PC.");
            copy_cap(out->advice, sizeof(out->advice),
                     "Disassemble at addr; check self-modifying code or wrong ISA.");
            break;
        case 7: /* SIGBUS */
            copy_cap(out->summary, sizeof(out->summary), "Bus error: bad alignment or mapping.");
            copy_cap(out->advice, sizeof(out->advice), "Check maps protect bits and alignment.");
            break;
        case 8: /* SIGFPE */
            copy_cap(out->summary, sizeof(out->summary), "Floating-point / arithmetic exception.");
            copy_cap(out->advice, sizeof(out->advice), "Inspect divisor and MXCSR/x87 state.");
            break;
        case 5: /* SIGTRAP */
            copy_cap(out->summary, sizeof(out->summary),
                     "Trap (breakpoint or single-step). Often expected under ptrace.");
            copy_cap(out->advice, sizeof(out->advice),
                     "If unexpected, check software BPs and TF; else continue.");
            break;
        case 6: /* SIGABRT */
            copy_cap(out->summary, sizeof(out->summary), "Abort signal (assert / abort()).");
            copy_cap(out->advice, sizeof(out->advice), "Walk bt; look for failed checks.");
            break;
        case 9: /* SIGKILL */
            copy_cap(out->summary, sizeof(out->summary), "SIGKILL: process was force-killed.");
            copy_cap(out->advice, sizeof(out->advice), "Cannot catch; check who sent the kill.");
            break;
        default:
            snprintf(out->summary, sizeof(out->summary), "Linux signal %s (%d).", out->name,
                     (int)code);
            copy_cap(out->advice, sizeof(out->advice),
                     "Use exlist/expass policy; see signals for disposition.");
            break;
    }
}

static void fill_win(int32_t code, re_ai_exsum_t *out) {
    uint32_t u = (uint32_t)code;
    switch (u) {
        case 0xC0000005u: /* ACCESS_VIOLATION */
            copy_cap(out->name, sizeof(out->name), "ACCESS_VIOLATION");
            copy_cap(out->summary, sizeof(out->summary),
                     "Access violation: read/write/execute of invalid address.");
            copy_cap(out->advice, sizeof(out->advice),
                     "Check ExceptionAddress and maps; bpm on the page if needed.");
            break;
        case 0xC000001Du: /* ILLEGAL_INSTRUCTION */
            copy_cap(out->name, sizeof(out->name), "ILLEGAL_INSTRUCTION");
            copy_cap(out->summary, sizeof(out->summary), "CPU rejected the opcode at the fault PC.");
            copy_cap(out->advice, sizeof(out->advice), "Disassemble at addr; verify code integrity.");
            break;
        case 0xC0000094u: /* INTEGER_DIVIDE_BY_ZERO */
            copy_cap(out->name, sizeof(out->name), "INT_DIVIDE_BY_ZERO");
            copy_cap(out->summary, sizeof(out->summary), "Integer divide by zero.");
            copy_cap(out->advice, sizeof(out->advice), "Inspect divisor register before the fault.");
            break;
        case 0x80000003u: /* BREAKPOINT */
            copy_cap(out->name, sizeof(out->name), "BREAKPOINT");
            copy_cap(out->summary, sizeof(out->summary),
                     "Breakpoint exception (int3 / debug break).");
            copy_cap(out->advice, sizeof(out->advice), "Expected at software BPs; else scan for int3.");
            break;
        case 0x80000004u: /* SINGLE_STEP */
            copy_cap(out->name, sizeof(out->name), "SINGLE_STEP");
            copy_cap(out->summary, sizeof(out->summary), "Single-step exception (trap flag).");
            copy_cap(out->advice, sizeof(out->advice), "Normal during sti; clear TF if stuck.");
            break;
        case 0xC00000FDu: /* STACK_OVERFLOW */
            copy_cap(out->name, sizeof(out->name), "STACK_OVERFLOW");
            copy_cap(out->summary, sizeof(out->summary), "Stack overflow / guard page hit.");
            copy_cap(out->advice, sizeof(out->advice), "Inspect RSP and recursion; grow stack carefully.");
            break;
        default:
            snprintf(out->name, sizeof(out->name), "0x%08X", u);
            snprintf(out->summary, sizeof(out->summary), "Windows exception code 0x%08X.", u);
            copy_cap(out->advice, sizeof(out->advice),
                     "Use exbp/exignore/expass; see caps for first-chance support.");
            break;
    }
}

re_err_code_t re_ai_summarize_exception(const re_dbg_event_t *ev, int32_t code_override,
                                        bool have_code, re_ai_exsum_t *out) {
    int32_t code = 0;
    if (!out)
        return RE_E_USAGE;
    memset(out, 0, sizeof(*out));
    if (ev && ev->kind == RE_DBG_EV_EXCEPTION) {
        out->from_event = true;
        out->addr = ev->addr;
        out->first_chance = ev->first_chance;
        code = ev->code;
    }
    if (have_code)
        code = code_override;
    if (!have_code && !(ev && ev->kind == RE_DBG_EV_EXCEPTION))
        return RE_E_USAGE;
    out->code = code;
    if (is_linux_signo(code))
        fill_linux(code, out);
    else
        fill_win(code, out);
    return RE_OK;
}
