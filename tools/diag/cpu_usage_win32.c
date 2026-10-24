/*
 * V9XCPU.EXE: logs how busy the CPU is, once a second, while a benchmark
 * runs - to tell driver overhead on the CPU from time spent waiting for the
 * GPU (docs\plans\d3d-core-throughput.md).
 *
 * Usage: V9XCPU [seconds]   (default 900; at most 7200)
 *
 * Each sample is one line of C:\V9XDIAG\V9XCPU.TXT:
 *   tick cpu tsc t0 .. t11 calls0 calls6 title
 * - tick:  GetTickCount at the sample;
 * - cpu:   Windows 98's own KERNEL\CPUUsage, percent, from HKEY_DYN_DATA
 *          (the figure System Monitor shows). A busy-wait counts as busy;
 * - tsc:   TSC cycles since the previous sample (low 32 bits);
 * - t0-11: cycles spent in each of the HAL's timing buckets since the
 *          previous sample (V9X_TIME_* in ddhal_internal.h, the
 *          V9X_DDGETTRACE snapshot), or all 0 under another driver;
 * - calls0, calls6: entries to the D3D-call and Flip buckets since then;
 * - title: the foreground window's title.
 * Differences are taken on the low 32 bits, which hold a second's cycles
 * on any CPU this runs on. The file is written through, so a machine that
 * locks keeps every sample before it.
 *
 * The sampling thread runs time-critical so a busy benchmark cannot delay
 * it; the work per sample is one registry read and one escape.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/win9x_ddraw_abi.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_CPU_PATH          V9X_DIAG_PATH("V9XCPU.TXT")
#define V9X_CPU_DEFAULT_S     900ul
#define V9X_CPU_MAX_S         7200ul
#define V9X_CPU_PERIOD_MS     1000ul
#define V9X_CPU_BUCKETS       12ul
/* V9X_TIME_* in ddhal_internal.h: D3D entry points, and Flip. */
#define V9X_CPU_BUCKET_D3D    0ul
#define V9X_CPU_BUCKET_FLIP   6ul

/* Windows 98 performance statistics: reading a value under StartStat
 * starts its collection, StatData reads it, StopStat ends it. */
#define V9X_CPU_START_KEY     "PerfStats\\StartStat"
#define V9X_CPU_DATA_KEY      "PerfStats\\StatData"
#define V9X_CPU_STOP_KEY      "PerfStats\\StopStat"
#define V9X_CPU_VALUE         "KERNEL\\CPUUsage"

typedef LONG (WINAPI *V9X_CPU_REG_OPEN)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *V9X_CPU_REG_QUERY)(HKEY, LPCSTR, LPDWORD, LPDWORD,
                                         LPBYTE, LPDWORD);
typedef LONG (WINAPI *V9X_CPU_REG_CLOSE)(HKEY);

static V9X_CPU_REG_OPEN v9x_cpu_reg_open;
static V9X_CPU_REG_QUERY v9x_cpu_reg_query;
static V9X_CPU_REG_CLOSE v9x_cpu_reg_close;

/* The 16-bit side copies its whole current snapshot into the buffer an
 * escape names - Control() is not told the buffer's size - so a driver
 * whose diagnostics grew writes past one sized for this build's header:
 * V9XCPU built before 2026-10-24's lock counters died with an illegal
 * operation under a driver with them. The slack takes such a copy; the
 * dwSize check then reads it as no buckets. */
#define V9X_CPU_SNAPSHOT_SLACK 0x10000ul
static union {
    V9X_DD_TRACE_SNAPSHOT snapshot;
    BYTE bytes[sizeof(V9X_DD_TRACE_SNAPSHOT) + V9X_CPU_SNAPSHOT_SLACK];
} v9x_cpu_buffer;
#define v9x_cpu_snapshot (v9x_cpu_buffer.snapshot)
static DWORD v9x_cpu_cycles[V9X_CPU_BUCKETS];
static DWORD v9x_cpu_calls[V9X_CPU_BUCKETS];

static DWORD v9x_cpu_rdtsc_low(void);
#pragma aux v9x_cpu_rdtsc_low = 0x0f 0x31 value [eax] modify exact [eax edx];

/* ADVAPI32 by name, as V9XTRACE loads it: the tool's static imports stay
 * KERNEL32, USER32 and GDI32. */
static int v9x_cpu_reg_load(void)
{
    HMODULE module = LoadLibraryA("ADVAPI32.DLL");

    if (module == 0) {
        return 0;
    }
    v9x_cpu_reg_query =
        (V9X_CPU_REG_QUERY)GetProcAddress(module, "RegQueryValueExA");
    v9x_cpu_reg_close = (V9X_CPU_REG_CLOSE)GetProcAddress(module,
                                                          "RegCloseKey");
    v9x_cpu_reg_open = (V9X_CPU_REG_OPEN)GetProcAddress(module,
                                                        "RegOpenKeyExA");
    return v9x_cpu_reg_query != 0 && v9x_cpu_reg_close != 0 &&
           v9x_cpu_reg_open != 0;
}

/* One DWORD statistic under key; 0xffffffff when it cannot be read. */
static DWORD v9x_cpu_stat(const char *key_name)
{
    HKEY key;
    DWORD value = 0xfffffffful;
    DWORD size = sizeof(value);
    DWORD type;

    if (v9x_cpu_reg_open(HKEY_DYN_DATA, key_name, 0ul, KEY_READ, &key) !=
            ERROR_SUCCESS) {
        return 0xfffffffful;
    }
    if (v9x_cpu_reg_query(key, V9X_CPU_VALUE, 0, &type, (LPBYTE)&value,
                          &size) != ERROR_SUCCESS) {
        value = 0xfffffffful;
    }
    v9x_cpu_reg_close(key);
    return value;
}

/* The HAL's buckets now; 0 under a driver without the escape. */
static int v9x_cpu_read_hal(void)
{
    V9X_DCICMD command;
    HDC screen;
    int result;

    command.dwCommand = V9X_DDGETTRACE;
    command.dwParam1 = 0ul;
    command.dwParam2 = 0ul;
    command.dwVersion = V9X_DD_VERSION;
    command.dwReserved = 0ul;
    screen = GetDC(0);
    if (screen == 0) {
        return 0;
    }
    result = ExtEscape(screen, V9X_DCICOMMAND, sizeof(command),
                       (LPCSTR)&command, sizeof(v9x_cpu_buffer),
                       (LPSTR)&v9x_cpu_snapshot);
    ReleaseDC(0, screen);
    return result > 0 &&
           v9x_cpu_snapshot.dwSize == sizeof(V9X_DD_TRACE_SNAPSHOT) &&
           v9x_cpu_snapshot.abi == V9X_DD_SHARED_ABI;
}

static DWORD v9x_cpu_seconds(void)
{
    const char *line = GetCommandLineA();
    DWORD seconds = 0ul;

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
    while (*line >= '0' && *line <= '9') {
        seconds = seconds * 10ul + (DWORD)(*line - '0');
        if (seconds > V9X_CPU_MAX_S) {
            return V9X_CPU_MAX_S;
        }
        ++line;
    }
    return seconds != 0ul ? seconds : V9X_CPU_DEFAULT_S;
}

static void v9x_cpu_write(HANDLE file, const char *text, int length)
{
    DWORD written;

    WriteFile(file, text, (DWORD)length, &written, 0);
}

void __stdcall V9xCpuUsageEntry(void)
{
    HANDLE file;
    char line[512];
    char title[128];
    DWORD seconds;
    DWORD sample;
    DWORD tsc;
    DWORD tsc_last;
    DWORD bucket;
    DWORD cpu;
    DWORD now;
    DWORD calls_d3d;
    DWORD calls_flip;
    int length;
    int hal;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_CPU_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(1ul);
    }
    if (!v9x_cpu_reg_load()) {
        v9x_cpu_write(file, "error no-advapi32\r\n", 19);
        ExitProcess(2ul);
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    seconds = v9x_cpu_seconds();
    (void)v9x_cpu_stat(V9X_CPU_START_KEY);

    hal = v9x_cpu_read_hal();
    for (bucket = 0ul; bucket < V9X_CPU_BUCKETS; ++bucket) {
        v9x_cpu_cycles[bucket] = hal ?
            v9x_cpu_snapshot.d3d.time_cycles[bucket * 2ul] : 0ul;
        v9x_cpu_calls[bucket] = hal ? v9x_cpu_snapshot.d3d.time_calls[bucket]
                                    : 0ul;
    }
    length = wsprintfA(line, "# V9XCPU build=%s hal=%d seconds=%lu\r\n",
                       V9X_BUILD_ID, hal, seconds);
    v9x_cpu_write(file, line, length);
    tsc_last = v9x_cpu_rdtsc_low();

    for (sample = 0ul; sample < seconds; ++sample) {
        Sleep(V9X_CPU_PERIOD_MS);
        now = GetTickCount();
        tsc = v9x_cpu_rdtsc_low();
        cpu = v9x_cpu_stat(V9X_CPU_DATA_KEY);
        length = wsprintfA(line, "%lu %lu %lu", now, cpu, tsc - tsc_last);
        tsc_last = tsc;

        /* Bucket differences: a snapshot that fails reads as no time. */
        hal = v9x_cpu_read_hal();
        for (bucket = 0ul; bucket < V9X_CPU_BUCKETS; ++bucket) {
            DWORD cycles = hal ?
                v9x_cpu_snapshot.d3d.time_cycles[bucket * 2ul] :
                v9x_cpu_cycles[bucket];

            length += wsprintfA(line + length, " %lu",
                                cycles - v9x_cpu_cycles[bucket]);
            v9x_cpu_cycles[bucket] = cycles;
        }
        calls_d3d = hal ?
            v9x_cpu_snapshot.d3d.time_calls[V9X_CPU_BUCKET_D3D] :
            v9x_cpu_calls[V9X_CPU_BUCKET_D3D];
        calls_flip = hal ?
            v9x_cpu_snapshot.d3d.time_calls[V9X_CPU_BUCKET_FLIP] :
            v9x_cpu_calls[V9X_CPU_BUCKET_FLIP];
        length += wsprintfA(line + length, " %lu %lu",
                            calls_d3d - v9x_cpu_calls[V9X_CPU_BUCKET_D3D],
                            calls_flip - v9x_cpu_calls[V9X_CPU_BUCKET_FLIP]);
        v9x_cpu_calls[V9X_CPU_BUCKET_D3D] = calls_d3d;
        v9x_cpu_calls[V9X_CPU_BUCKET_FLIP] = calls_flip;

        title[0] = '\0';
        GetWindowTextA(GetForegroundWindow(), title, sizeof(title));
        length += wsprintfA(line + length, " %s\r\n", title);
        v9x_cpu_write(file, line, length);
    }
    (void)v9x_cpu_stat(V9X_CPU_STOP_KEY);
    CloseHandle(file);
    ExitProcess(0ul);
}
