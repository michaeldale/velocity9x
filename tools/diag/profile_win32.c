/*
 * V9XPROF.EXE: a sampling profiler for one program's main thread, to find
 * where a benchmark's CPU time goes outside the HAL's own timing buckets -
 * the Direct3D runtime, DirectDraw, the 16-bit driver and the thunks to it
 * (docs\plans\d3d-core-throughput.md) - and where a game's load time goes.
 *
 * Usage: V9XPROF <program> [arguments]
 *
 * It starts the program itself, from the program's own folder: Windows 98
 * has no OpenThread, and CreateProcess is the one way to a handle on
 * another process's thread. About once a millisecond it suspends that
 * thread, reads its context and resumes it. A thread inside a VxD is
 * suspended on its way back to ring 3, so a sample is where ring 3 was.
 *
 * Writes, once the program ends or the buffer fills:
 *   C:\V9XDIAG\V9XPROF.BIN  one record a sample: tick, eip, cs (3 DWORDs)
 *   C:\V9XDIAG\V9XPROF.TXT  the program's 32-bit modules as they appeared
 *                            (toolhelp, polled once a second), the base of
 *                            every 16-bit code selector seen, and totals
 * The host resolves addresses against those (scripts\report-profile.ps1).
 * Only the main thread is seen: a program that draws from another thread
 * shows its main thread waiting in USER32.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_PROF_BIN_PATH      V9X_DIAG_PATH("V9XPROF.BIN")
#define V9X_PROF_TXT_PATH      V9X_DIAG_PATH("V9XPROF.TXT")
/* 1,048,576 samples: over 17 minutes at a millisecond. */
#define V9X_PROF_MAX_SAMPLES   0x100000ul
#define V9X_PROF_RECORD_DWORDS 3ul
#define V9X_PROF_MAX_MODULES   256ul
#define V9X_PROF_MAX_SELECTORS 64ul
#define V9X_PROF_MODULE_POLL_MS 1000ul

typedef UINT (WINAPI *V9X_PROF_TIME_PERIOD)(UINT);
typedef HANDLE (WINAPI *V9X_PROF_SNAPSHOT)(DWORD, DWORD);
typedef BOOL (WINAPI *V9X_PROF_MODULE_WALK)(HANDLE, LPMODULEENTRY32);

static DWORD *v9x_prof_samples;
static DWORD v9x_prof_count;
static DWORD v9x_prof_module_base[V9X_PROF_MAX_MODULES];
static DWORD v9x_prof_module_count;
static WORD v9x_prof_selector[V9X_PROF_MAX_SELECTORS];
static DWORD v9x_prof_selector_count;
static HANDLE v9x_prof_text;
static V9X_PROF_SNAPSHOT v9x_prof_snapshot;
static V9X_PROF_MODULE_WALK v9x_prof_module_first;
static V9X_PROF_MODULE_WALK v9x_prof_module_next;

static WORD v9x_prof_own_cs(void);
#pragma aux v9x_prof_own_cs = "mov ax,cs" value [ax];

static void v9x_prof_write(const char *text, int length)
{
    DWORD written;

    WriteFile(v9x_prof_text, text, (DWORD)length, &written, 0);
}

/* The program's command line: ours past our own name, quoted or not. */
static const char *v9x_prof_target(void)
{
    const char *line = GetCommandLineA();

    if (*line == '"') {
        ++line;
        while (*line != '\0' && *line != '"') {
            ++line;
        }
        if (*line == '"') {
            ++line;
        }
    } else {
        while (*line != '\0' && *line != ' ') {
            ++line;
        }
    }
    while (*line == ' ') {
        ++line;
    }
    return line;
}

/* The folder of the program the command line names, for its working
 * directory: games load their data relative to it. */
static void v9x_prof_folder(const char *command, char *folder, DWORD size)
{
    DWORD length = 0ul;
    DWORD last_slash = 0ul;
    char end = ' ';

    if (*command == '"') {
        ++command;
        end = '"';
    }
    while (command[length] != '\0' && command[length] != end &&
           length + 1ul < size) {
        folder[length] = command[length];
        if (command[length] == '\\') {
            last_slash = length;
        }
        ++length;
    }
    folder[last_slash] = '\0';
}

/* Every 32-bit module of the process not listed yet, with when. */
static void v9x_prof_poll_modules(DWORD process_id, DWORD tick)
{
    HANDLE snapshot;
    MODULEENTRY32 entry;
    char line[400];
    DWORD index;
    int length;

    if (v9x_prof_snapshot == 0) {
        return;
    }
    snapshot = v9x_prof_snapshot(TH32CS_SNAPMODULE, process_id);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    entry.dwSize = sizeof(entry);
    if (v9x_prof_module_first(snapshot, &entry)) {
        do {
            DWORD base = (DWORD)entry.modBaseAddr;

            for (index = 0ul; index < v9x_prof_module_count; ++index) {
                if (v9x_prof_module_base[index] == base) {
                    break;
                }
            }
            if (index == v9x_prof_module_count &&
                v9x_prof_module_count < V9X_PROF_MAX_MODULES) {
                v9x_prof_module_base[v9x_prof_module_count++] = base;
                length = wsprintfA(line, "module %lu %08lX %08lX %s\r\n",
                                   tick, base, entry.modBaseSize,
                                   entry.szExePath);
                v9x_prof_write(line, length);
            }
            entry.dwSize = sizeof(entry);
        } while (v9x_prof_module_next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

/* A 16-bit code selector's base, recorded once. */
static void v9x_prof_note_selector(HANDLE thread, WORD selector)
{
    LDT_ENTRY entry;
    char line[96];
    DWORD index;
    DWORD base = 0xfffffffful;
    DWORD limit = 0ul;
    int length;

    for (index = 0ul; index < v9x_prof_selector_count; ++index) {
        if (v9x_prof_selector[index] == selector) {
            return;
        }
    }
    if (v9x_prof_selector_count >= V9X_PROF_MAX_SELECTORS) {
        return;
    }
    v9x_prof_selector[v9x_prof_selector_count++] = selector;
    if (GetThreadSelectorEntry(thread, selector, &entry)) {
        base = (DWORD)entry.BaseLow |
               ((DWORD)entry.HighWord.Bytes.BaseMid << 16) |
               ((DWORD)entry.HighWord.Bytes.BaseHi << 24);
        limit = (DWORD)entry.LimitLow |
                ((DWORD)(entry.HighWord.Bytes.Flags2 & 0x0fu) << 16);
    }
    length = wsprintfA(line, "selector %04X %08lX %08lX\r\n",
                       (unsigned)selector, base, limit);
    v9x_prof_write(line, length);
}

void __stdcall V9xProfileEntry(void)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    CONTEXT context;
    HMODULE module;
    V9X_PROF_TIME_PERIOD begin_period = 0;
    V9X_PROF_TIME_PERIOD end_period = 0;
    HANDLE binary;
    char command[512];
    char folder[MAX_PATH];
    char line[256];
    const char *target;
    DWORD index;
    DWORD started;
    DWORD next_poll;
    DWORD now;
    DWORD lost = 0ul;
    DWORD written;
    WORD flat_cs = v9x_prof_own_cs();
    int length;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    v9x_prof_text = CreateFileA(V9X_PROF_TXT_PATH, GENERIC_WRITE,
                                FILE_SHARE_READ, 0, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, 0);
    if (v9x_prof_text == INVALID_HANDLE_VALUE) {
        ExitProcess(1ul);
    }
    target = v9x_prof_target();
    length = wsprintfA(line, "# V9XPROF build=%s\r\n", V9X_BUILD_ID);
    v9x_prof_write(line, length);
    if (*target == '\0' || lstrlenA(target) >= (int)sizeof(command)) {
        v9x_prof_write("error usage\r\n", 13);
        ExitProcess(2ul);
    }
    v9x_prof_samples = (DWORD *)VirtualAlloc(
        0, V9X_PROF_MAX_SAMPLES * V9X_PROF_RECORD_DWORDS * sizeof(DWORD),
        MEM_COMMIT, PAGE_READWRITE);
    if (v9x_prof_samples == 0) {
        v9x_prof_write("error no-memory\r\n", 17);
        ExitProcess(3ul);
    }

    /* Toolhelp and the multimedia timer by name: the tool's static imports
     * stay KERNEL32, USER32 and GDI32. */
    module = GetModuleHandleA("KERNEL32.DLL");
    v9x_prof_snapshot = (V9X_PROF_SNAPSHOT)GetProcAddress(
        module, "CreateToolhelp32Snapshot");
    v9x_prof_module_first = (V9X_PROF_MODULE_WALK)GetProcAddress(
        module, "Module32First");
    v9x_prof_module_next = (V9X_PROF_MODULE_WALK)GetProcAddress(
        module, "Module32Next");
    if (v9x_prof_module_first == 0 || v9x_prof_module_next == 0) {
        v9x_prof_snapshot = 0;
    }
    module = LoadLibraryA("WINMM.DLL");
    if (module != 0) {
        begin_period = (V9X_PROF_TIME_PERIOD)GetProcAddress(
            module, "timeBeginPeriod");
        end_period = (V9X_PROF_TIME_PERIOD)GetProcAddress(
            module, "timeEndPeriod");
    }
    if (begin_period != 0) {
        begin_period(1u);
    }

    lstrcpyA(command, target);
    v9x_prof_folder(target, folder, sizeof(folder));
    length = wsprintfA(line, "target %s\r\nfolder %s\r\nflatcs %04X\r\n",
                       command, folder, (unsigned)flat_cs);
    v9x_prof_write(line, length);
    for (index = 0ul; index < sizeof(startup); ++index) {
        ((BYTE *)&startup)[index] = 0u;
    }
    startup.cb = sizeof(startup);
    if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0,
                        folder[0] != '\0' ? folder : 0, &startup,
                        &process)) {
        length = wsprintfA(line, "error create %lu\r\n", GetLastError());
        v9x_prof_write(line, length);
        ExitProcess(4ul);
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    started = GetTickCount();
    length = wsprintfA(line, "start %lu pid %08lX\r\n", started,
                       process.dwProcessId);
    v9x_prof_write(line, length);
    next_poll = started;

    while (v9x_prof_count < V9X_PROF_MAX_SAMPLES) {
        DWORD *record;

        Sleep(1ul);
        if (WaitForSingleObject(process.hProcess, 0ul) == WAIT_OBJECT_0) {
            break;
        }
        now = GetTickCount();
        if ((long)(now - next_poll) >= 0l) {
            v9x_prof_poll_modules(process.dwProcessId, now - started);
            next_poll = now + V9X_PROF_MODULE_POLL_MS;
        }
        if (SuspendThread(process.hThread) == 0xfffffffful) {
            break;
        }
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(process.hThread, &context)) {
            ResumeThread(process.hThread);
            ++lost;
            continue;
        }
        ResumeThread(process.hThread);
        record = &v9x_prof_samples[v9x_prof_count * V9X_PROF_RECORD_DWORDS];
        record[0] = now - started;
        record[1] = context.Eip;
        record[2] = context.SegCs & 0xfffful;
        ++v9x_prof_count;
        if ((WORD)context.SegCs != flat_cs) {
            v9x_prof_note_selector(process.hThread, (WORD)context.SegCs);
        }
    }

    now = GetTickCount();
    if (end_period != 0) {
        end_period(1u);
    }
    length = wsprintfA(line, "end %lu samples %lu lost %lu\r\n",
                       now - started, v9x_prof_count, lost);
    v9x_prof_write(line, length);
    CloseHandle(v9x_prof_text);
    binary = CreateFileA(V9X_PROF_BIN_PATH, GENERIC_WRITE, 0, 0,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (binary != INVALID_HANDLE_VALUE) {
        WriteFile(binary, v9x_prof_samples,
                  v9x_prof_count * V9X_PROF_RECORD_DWORDS * sizeof(DWORD),
                  &written, 0);
        CloseHandle(binary);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ExitProcess(0ul);
}
