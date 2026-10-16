// re_dbg_os_win.c - Windows SEH chain walk and process handle enumeration.
// Module: feature/os (C11).
// Owns: TEB ExceptionList walk and SystemHandleInformation filter by pid.
// Depends: re_dbg_os.h. Live on Windows; returns RE_E_UNSUPPORTED elsewhere.
#include "features/os/re_dbg_os.h"

#include <string.h>

#if defined(_WIN32)

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI *NtQueryInformationThread_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI *NtQuerySystemInformation_fn)(ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI *NtQueryObject_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);

#    define RE_ThreadBasicInformation 0ul
#    define RE_SystemHandleInformation 16ul
#    define RE_ObjectNameInformation 1ul

typedef struct {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} re_client_id_t;

typedef struct {
    NTSTATUS ExitStatus;
    PVOID TebBaseAddress;
    re_client_id_t ClientId;
    ULONG_PTR AffinityMask;
    LONG Priority;
    LONG BasePriority;
} re_thread_basic_info_t;

typedef struct {
    ULONG_PTR UniqueProcessId;
    UCHAR ObjectTypeIndex;
    UCHAR HandleAttributes;
    USHORT HandleValue;
    PVOID Object;
    ULONG GrantedAccess;
} re_sys_handle_entry_t;

typedef struct {
    ULONG_PTR NumberOfHandles;
    re_sys_handle_entry_t Handles[1];
} re_sys_handle_info_t;

typedef struct {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} re_unicode_str_t;

typedef struct {
    re_unicode_str_t Name;
} re_object_name_info_t;

static void copy_cstr(char *dst, size_t cap, const char *src) {
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

static void wide_to_utf8(char *dst, size_t cap, const WCHAR *src, size_t nchars) {
    size_t i = 0, o = 0;
    if (!dst || cap == 0)
        return;
    dst[0] = 0;
    if (!src)
        return;
    for (i = 0; i < nchars && o + 1u < cap; i++) {
        WCHAR c = src[i];
        if (c < 0x80)
            dst[o++] = (char)c;
        else if (c < 0x800 && o + 2u < cap) {
            dst[o++] = (char)(0xc0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3f));
        } else if (o + 3u < cap) {
            dst[o++] = (char)(0xe0 | (c >> 12));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            dst[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    dst[o] = 0;
}

static bool read_u64(HANDLE proc, uint64_t addr, uint64_t *out) {
    SIZE_T got = 0;
    if (!out)
        return false;
    *out = 0;
    return ReadProcessMemory(proc, (LPCVOID)(uintptr_t)addr, out, sizeof(*out), &got) &&
           got == sizeof(*out);
}

static size_t walk_seh_chain(HANDLE proc, uint32_t tid, uint64_t head, re_dbg_seh_frame_t *out,
                             size_t cap, size_t n) {
    uint64_t cur = head;
    size_t depth = 0;
    while (cur && cur != (uint64_t)~0ull && depth < 32 && n < RE_DBG_SEH_MAX) {
        uint64_t next = 0, handler = 0;
        if (!read_u64(proc, cur, &next) || !read_u64(proc, cur + 8u, &handler))
            break;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].tid = tid;
            out[n].frame = cur;
            out[n].handler = handler;
            out[n].next = next;
        }
        n++;
        depth++;
        if (next == cur)
            break;
        cur = next;
    }
    return n;
}

static size_t seh_for_tid(HANDLE proc, NtQueryInformationThread_fn pQit, uint32_t tid,
                          re_dbg_seh_frame_t *out, size_t cap, size_t n) {
    HANDLE th;
    re_thread_basic_info_t tbi;
    NTSTATUS st;
    uint64_t teb = 0, cur = 0;
    memset(&tbi, 0, sizeof(tbi));
    th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, (DWORD)tid);
    if (!th)
        return n;
    st = pQit(th, RE_ThreadBasicInformation, &tbi, sizeof(tbi), NULL);
    CloseHandle(th);
    if (st < 0)
        return n;
    teb = (uint64_t)(uintptr_t)tbi.TebBaseAddress;
    if (!teb || !read_u64(proc, teb, &cur))
        return n;
    return walk_seh_chain(proc, tid, cur, out, cap, n);
}

re_err_code_t re_dbg_session_enum_seh(re_dbg_session_t *s, re_dbg_seh_frame_t *out, size_t cap,
                                      size_t *n_out) {
    re_dbg_thread_t thr[64];
    size_t nt = 0, n = 0, ti;
    HANDLE proc;
    HMODULE ntdll;
    NtQueryInformationThread_fn pQit;
    if (n_out)
        *n_out = 0;
    if (!s || !s->attached || s->pid == 0)
        return RE_E_USAGE;
    if (re_dbg_session_enum_threads(s, thr, 64, &nt) != RE_OK || nt == 0)
        return RE_E_IO;
    ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return RE_E_UNSUPPORTED;
    pQit = (NtQueryInformationThread_fn)GetProcAddress(ntdll, "NtQueryInformationThread");
    if (!pQit)
        return RE_E_UNSUPPORTED;
    proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)s->pid);
    if (!proc)
        return RE_E_IO;
    for (ti = 0; ti < nt && n < RE_DBG_SEH_MAX; ti++)
        n = seh_for_tid(proc, pQit, thr[ti].tid, out, cap, n);
    CloseHandle(proc);
    if (n_out)
        *n_out = n;
    return RE_OK;
}

static void type_name_for(uint32_t idx, char *out, size_t cap) {
    const char *n = "Object";
    switch (idx) {
        case 0x07:
            n = "Process";
            break;
        case 0x08:
            n = "Thread";
            break;
        case 0x0a:
            n = "Event";
            break;
        case 0x11:
            n = "File";
            break;
        case 0x13:
            n = "Section";
            break;
        case 0x14:
            n = "Key";
            break;
        case 0x24:
            n = "Mutant";
            break;
        default:
            break;
    }
    copy_cstr(out, cap, n);
}

static void fill_handle_name(HANDLE self, NtQueryObject_fn pQo, USHORT hv, char *dst, size_t cap) {
    HANDLE dup = NULL;
    uint8_t namebuf[512];
    ULONG ret = 0;
    if (!self || !pQo || !dst || cap == 0)
        return;
    dst[0] = 0;
    if (!DuplicateHandle(self, (HANDLE)(uintptr_t)hv, GetCurrentProcess(), &dup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return;
    memset(namebuf, 0, sizeof(namebuf));
    if (pQo(dup, RE_ObjectNameInformation, namebuf, sizeof(namebuf), &ret) == 0) {
        re_object_name_info_t *ni = (re_object_name_info_t *)namebuf;
        if (ni->Name.Buffer && ni->Name.Length)
            wide_to_utf8(dst, cap, ni->Name.Buffer, ni->Name.Length / sizeof(WCHAR));
    }
    CloseHandle(dup);
}

static uint8_t *query_handle_table(NtQuerySystemInformation_fn pQsi, ULONG *buf_sz_out) {
    ULONG buf_sz = 1u << 20;
    ULONG need = 0;
    uint8_t *buf = (uint8_t *)VirtualAlloc(NULL, buf_sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf)
        return NULL;
    for (;;) {
        NTSTATUS st = pQsi(RE_SystemHandleInformation, buf, buf_sz, &need);
        if (st == 0) {
            if (buf_sz_out)
                *buf_sz_out = buf_sz;
            return buf;
        }
        if ((ULONG)st != 0xC0000004u || need <= buf_sz) {
            VirtualFree(buf, 0, MEM_RELEASE);
            return NULL;
        }
        VirtualFree(buf, 0, MEM_RELEASE);
        buf_sz = need + (1u << 16);
        buf = (uint8_t *)VirtualAlloc(NULL, buf_sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf)
            return NULL;
    }
}

re_err_code_t re_dbg_session_enum_handles(re_dbg_session_t *s, re_dbg_handle_t *out, size_t cap,
                                          size_t *n_out) {
    HMODULE ntdll;
    NtQuerySystemInformation_fn pQsi;
    NtQueryObject_fn pQo;
    uint8_t *buf;
    re_sys_handle_info_t *info;
    size_t n = 0, i;
    HANDLE self;
    if (n_out)
        *n_out = 0;
    if (!s || !s->attached || s->pid == 0)
        return RE_E_USAGE;
    ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return RE_E_UNSUPPORTED;
    pQsi = (NtQuerySystemInformation_fn)GetProcAddress(ntdll, "NtQuerySystemInformation");
    pQo = (NtQueryObject_fn)GetProcAddress(ntdll, "NtQueryObject");
    if (!pQsi)
        return RE_E_UNSUPPORTED;
    buf = query_handle_table(pQsi, NULL);
    if (!buf)
        return RE_E_IO;
    info = (re_sys_handle_info_t *)buf;
    self = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION, FALSE, (DWORD)s->pid);
    for (i = 0; i < (size_t)info->NumberOfHandles && n < RE_DBG_HANDLE_MAX; i++) {
        re_sys_handle_entry_t *e = &info->Handles[i];
        if ((uint32_t)e->UniqueProcessId != s->pid)
            continue;
        if (out && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].handle = (uint64_t)e->HandleValue;
            out[n].type_index = e->ObjectTypeIndex;
            out[n].granted_access = e->GrantedAccess;
            type_name_for(e->ObjectTypeIndex, out[n].type_name, sizeof(out[n].type_name));
            fill_handle_name(self, pQo, e->HandleValue, out[n].object_name,
                             sizeof(out[n].object_name));
        }
        n++;
    }
    if (self)
        CloseHandle(self);
    VirtualFree(buf, 0, MEM_RELEASE);
    if (n_out)
        *n_out = n;
    return RE_OK;
}

#else /* !_WIN32 */

re_err_code_t re_dbg_session_enum_seh(re_dbg_session_t *s, re_dbg_seh_frame_t *out, size_t cap,
                                      size_t *n_out) {
    (void)s;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}

re_err_code_t re_dbg_session_enum_handles(re_dbg_session_t *s, re_dbg_handle_t *out, size_t cap,
                                          size_t *n_out) {
    (void)s;
    (void)out;
    (void)cap;
    if (n_out)
        *n_out = 0;
    return RE_E_UNSUPPORTED;
}

#endif
