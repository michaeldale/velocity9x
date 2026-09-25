/*
 * Win16 mutex calibration probe (docs\plans\opengl-1.1-icd.md, Phase 0.2).
 *
 * Answers three questions about the guest before any driver code depends on
 * the answers: whether KERNEL32's ordinals 93, 96, 97 and 98 (GetpWin16Lock,
 * _ConfirmWin16Lock, _EnterSysLevel, _LeaveSysLevel) resolve through
 * GetProcAddress, through a walk of the export table, and to the same
 * addresses; what _ConfirmWin16Lock returns when the lock is and is not held
 * by this thread; and what the mutex structure itself looks like in each
 * state, so a reader that cannot trust the return value can read the
 * structure instead. The "held" states are two: this thread taking the lock
 * itself through _EnterSysLevel, and DirectDraw taking it on the thread's
 * behalf inside a primary-surface Lock, which the DDK documents as holding
 * the Win16 lock whatever DDLOCK_NOSYSLOCK says.
 *
 * Every step records its stage name before it runs, and the unhandled
 * exception filter writes the stage and the fault into the same INI, so a
 * call that crashes still leaves a record of which call it was. Nothing is
 * written while the lock is held: readings are captured to memory and written
 * after _LeaveSysLevel or Unlock.
 *
 * Writes C:\V9XDIAG\V9XW16L.INI. Imports KERNEL32 and USER32 only.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"

#define V9X_W16L_SECTION "Win16Lock"
#define V9X_W16L_CS_DWORDS 6ul

#define V9X_W16L_ORD_GETPWIN16LOCK  93u
#define V9X_W16L_ORD_CONFIRM        96u
#define V9X_W16L_ORD_ENTER          97u
#define V9X_W16L_ORD_LEAVE          98u

typedef void (WINAPI *V9X_W16L_GETPWIN16LOCK)(CRITICAL_SECTION **lock);
typedef DWORD (WINAPI *V9X_W16L_CONFIRM)(void);
typedef void (WINAPI *V9X_W16L_SYSLEVEL)(CRITICAL_SECTION *lock);
typedef HRESULT (WINAPI *V9X_W16L_DDRAW_CREATE)(GUID *, LPDIRECTDRAW *, IUnknown *);

/* One reading of the mutex, taken in a named state. */
typedef struct v9x_w16l_reading {
    const char *state;
    DWORD confirm;          /* _ConfirmWin16Lock's return, or 0xFFFFFFFF   */
    DWORD confirm_called;
    DWORD cs[V9X_W16L_CS_DWORDS];
    DWORD cs_readable;
} V9X_W16L_READING;

static const char *v9x_w16l_stage = "start";

static void v9x_w16l_text(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_W16L_SECTION, key, value, V9X_DIAG_W16L_INI);
}

static void v9x_w16l_hex(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "0x%08lX", value);
    v9x_w16l_text(key, text);
}

static void v9x_w16l_uint(const char *key, DWORD value)
{
    char text[16];

    wsprintfA(text, "%lu", value);
    v9x_w16l_text(key, text);
}

static void v9x_w16l_set_stage(const char *stage)
{
    v9x_w16l_stage = stage;
    v9x_w16l_text("Stage", stage);
}

/*
 * A fault anywhere after the filter is installed lands here, which turns a
 * dead process into a record: the stage that was running, the code and the
 * address. ExitProcess rather than returning, so the system's own dialog -
 * which would hold the Win16 mutex and wedge the agent - never appears.
 */
static LONG WINAPI v9x_w16l_filter(EXCEPTION_POINTERS *pointers)
{
    v9x_w16l_text("Result", "CRASH");
    v9x_w16l_text("CrashStage", v9x_w16l_stage);
    if (pointers != 0 && pointers->ExceptionRecord != 0) {
        v9x_w16l_hex("CrashCode", pointers->ExceptionRecord->ExceptionCode);
        v9x_w16l_hex("CrashAddress",
                     (DWORD)pointers->ExceptionRecord->ExceptionAddress);
    }
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_W16L_INI);
    ExitProcess(3u);
    return EXCEPTION_EXECUTE_HANDLER;
}

/*
 * The export-table walk: ordinal N of a loaded module is
 * AddressOfFunctions[N - Base], relative to the image base. This is the route
 * the HAL will take, because Win95's GetProcAddress is reported to refuse
 * ordinals into KERNEL32 (SciTech's k32exp.c does the same walk for the same
 * reason); whether 98SE's refuses is one of the things this probe records.
 * Returns 0 for an ordinal outside the table or one that is a forwarder.
 */
static FARPROC v9x_w16l_walk(HMODULE module, DWORD ordinal,
                             DWORD *base_out, DWORD *count_out)
{
    const BYTE *image = (const BYTE *)module;
    DWORD pe;
    DWORD optional;
    DWORD export_rva;
    DWORD export_size;
    const DWORD *export_dir;
    DWORD base;
    DWORD count;
    DWORD functions_rva;
    DWORD rva;

    if (image == 0 || IsBadReadPtr(image, 0x40)) {
        return 0;
    }
    pe = *(const DWORD *)(image + 0x3C);
    if (IsBadReadPtr(image + pe, 0x78 + 0x60)) {
        return 0;
    }
    if (*(const DWORD *)(image + pe) != 0x00004550ul) {
        return 0;
    }
    optional = pe + 24ul;
    export_rva = *(const DWORD *)(image + optional + 96ul);
    export_size = *(const DWORD *)(image + optional + 100ul);
    if (export_rva == 0ul || IsBadReadPtr(image + export_rva, 40)) {
        return 0;
    }
    export_dir = (const DWORD *)(image + export_rva);
    base = export_dir[4];
    count = export_dir[5];
    functions_rva = export_dir[7];
    *base_out = base;
    *count_out = count;
    if (ordinal < base || ordinal - base >= count) {
        return 0;
    }
    if (IsBadReadPtr(image + functions_rva, count * 4ul)) {
        return 0;
    }
    rva = ((const DWORD *)(image + functions_rva))[ordinal - base];
    if (rva == 0ul) {
        return 0;
    }
    /* A function RVA inside the export directory is a forwarder string. */
    if (rva >= export_rva && rva < export_rva + export_size) {
        return 0;
    }
    return (FARPROC)(image + rva);
}

static void v9x_w16l_read(V9X_W16L_READING *reading, const char *state,
                          CRITICAL_SECTION *lock, V9X_W16L_CONFIRM confirm,
                          int call_confirm)
{
    DWORD i;

    reading->state = state;
    reading->confirm = 0xFFFFFFFFul;
    reading->confirm_called = 0ul;
    reading->cs_readable = 0ul;
    for (i = 0ul; i < V9X_W16L_CS_DWORDS; ++i) {
        reading->cs[i] = 0ul;
    }
    if (lock != 0 && !IsBadReadPtr(lock, V9X_W16L_CS_DWORDS * 4ul)) {
        reading->cs_readable = 1ul;
        for (i = 0ul; i < V9X_W16L_CS_DWORDS; ++i) {
            reading->cs[i] = ((const DWORD *)lock)[i];
        }
    }
    if (call_confirm && confirm != 0) {
        reading->confirm_called = 1ul;
        reading->confirm = confirm();
    }
}

static void v9x_w16l_write_reading(const V9X_W16L_READING *reading)
{
    char key[48];
    DWORD i;

    wsprintfA(key, "%sConfirmCalled", reading->state);
    v9x_w16l_uint(key, reading->confirm_called);
    wsprintfA(key, "%sConfirm", reading->state);
    v9x_w16l_hex(key, reading->confirm);
    wsprintfA(key, "%sCsReadable", reading->state);
    v9x_w16l_uint(key, reading->cs_readable);
    for (i = 0ul; i < V9X_W16L_CS_DWORDS; ++i) {
        wsprintfA(key, "%sCs%lu", reading->state, i);
        v9x_w16l_hex(key, reading->cs[i]);
    }
}

static void v9x_w16l_zero(void *block, DWORD bytes)
{
    DWORD i;

    for (i = 0ul; i < bytes; ++i) {
        ((BYTE *)block)[i] = 0u;
    }
}

void __stdcall V9xWin16LockProbeEntry(void)
{
    HMODULE kernel32;
    DWORD ordinals[4];
    const char *names[4];
    FARPROC by_gpa[4];
    FARPROC by_walk[4];
    DWORD base = 0ul;
    DWORD count = 0ul;
    V9X_W16L_GETPWIN16LOCK get_lock = 0;
    V9X_W16L_CONFIRM confirm = 0;
    V9X_W16L_SYSLEVEL enter = 0;
    V9X_W16L_SYSLEVEL leave = 0;
    CRITICAL_SECTION *lock = 0;
    V9X_W16L_READING readings[8];
    DWORD reading_count = 0ul;
    DWORD i;
    int ok = 1;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_DIAG_W16L_INI);
    v9x_w16l_text("Build", V9X_BUILD_ID);
    v9x_w16l_text("Result", "RUNNING");
    v9x_w16l_uint("SchemaVersion", 1ul);
    v9x_w16l_hex("ThreadId", GetCurrentThreadId());
    v9x_w16l_hex("ProcessId", GetCurrentProcessId());
    SetUnhandledExceptionFilter(v9x_w16l_filter);

    ordinals[0] = V9X_W16L_ORD_GETPWIN16LOCK; names[0] = "GetpWin16Lock";
    ordinals[1] = V9X_W16L_ORD_CONFIRM;       names[1] = "ConfirmWin16Lock";
    ordinals[2] = V9X_W16L_ORD_ENTER;         names[2] = "EnterSysLevel";
    ordinals[3] = V9X_W16L_ORD_LEAVE;         names[3] = "LeaveSysLevel";

    v9x_w16l_set_stage("resolve");
    kernel32 = GetModuleHandleA("KERNEL32.DLL");
    v9x_w16l_hex("Kernel32Base", (DWORD)kernel32);
    for (i = 0ul; i < 4ul; ++i) {
        char key[48];

        by_gpa[i] = kernel32 != 0
            ? GetProcAddress(kernel32, (LPCSTR)(DWORD)ordinals[i]) : 0;
        by_walk[i] = v9x_w16l_walk(kernel32, ordinals[i], &base, &count);
        wsprintfA(key, "%sGpa", names[i]);
        v9x_w16l_hex(key, (DWORD)by_gpa[i]);
        wsprintfA(key, "%sWalk", names[i]);
        v9x_w16l_hex(key, (DWORD)by_walk[i]);
        wsprintfA(key, "%sAgree", names[i]);
        v9x_w16l_uint(key, (by_gpa[i] != 0 && by_gpa[i] == by_walk[i]) ? 1ul : 0ul);
    }
    v9x_w16l_uint("ExportOrdinalBase", base);
    v9x_w16l_uint("ExportFunctionCount", count);
    v9x_w16l_hex("GetProcAddressLastError", GetLastError());

    /* Prefer the walk, which is the HAL's route; fall back to GetProcAddress
     * so a guest where the walk fails still calibrates the rest. */
    get_lock = (V9X_W16L_GETPWIN16LOCK)(by_walk[0] != 0 ? by_walk[0] : by_gpa[0]);
    confirm = (V9X_W16L_CONFIRM)(by_walk[1] != 0 ? by_walk[1] : by_gpa[1]);
    enter = (V9X_W16L_SYSLEVEL)(by_walk[2] != 0 ? by_walk[2] : by_gpa[2]);
    leave = (V9X_W16L_SYSLEVEL)(by_walk[3] != 0 ? by_walk[3] : by_gpa[3]);

    v9x_w16l_set_stage("getpwin16lock");
    if (get_lock != 0) {
        get_lock(&lock);
    }
    v9x_w16l_hex("LockPointer", (DWORD)lock);

    v9x_w16l_set_stage("idle");
    v9x_w16l_read(&readings[reading_count++], "Idle", lock, confirm, 0);

    v9x_w16l_set_stage("confirm-idle");
    v9x_w16l_read(&readings[reading_count++], "IdleConfirmed", lock, confirm, 1);
    v9x_w16l_write_reading(&readings[0]);
    v9x_w16l_write_reading(&readings[1]);

    if (enter != 0 && leave != 0 && lock != 0) {
        V9X_W16L_READING held;
        V9X_W16L_READING held_confirmed;
        V9X_W16L_READING released;

        v9x_w16l_set_stage("entersyslevel");
        enter(lock);
        v9x_w16l_read(&held, "Held", lock, confirm, 0);
        v9x_w16l_read(&held_confirmed, "HeldConfirmed", lock, confirm, 1);
        leave(lock);
        v9x_w16l_read(&released, "Released", lock, confirm, 1);
        v9x_w16l_set_stage("entersyslevel-written");
        v9x_w16l_write_reading(&held);
        v9x_w16l_write_reading(&held_confirmed);
        v9x_w16l_write_reading(&released);
        v9x_w16l_uint("EnterLeaveDone", 1ul);
    } else {
        v9x_w16l_uint("EnterLeaveDone", 0ul);
    }

    /* DirectDraw's own hold: a primary-surface Lock. */
    {
        HMODULE module;
        V9X_W16L_DDRAW_CREATE create = 0;
        LPDIRECTDRAW dd = 0;
        LPDIRECTDRAWSURFACE primary = 0;
        LPDIRECTDRAWSURFACE offscreen = 0;
        DDSURFACEDESC desc;
        HRESULT hr;
        V9X_W16L_READING in_lock;
        V9X_W16L_READING after_unlock;
        V9X_W16L_READING in_nosys;
        DWORD dd_ok = 0ul;
        DWORD nosys_ok = 0ul;

        v9x_w16l_set_stage("ddraw-create");
        module = LoadLibraryA("DDRAW.DLL");
        create = module ? (V9X_W16L_DDRAW_CREATE)GetProcAddress(module, "DirectDrawCreate") : 0;
        if (create != 0 && create(0, &dd, 0) == DD_OK &&
            IDirectDraw_SetCooperativeLevel(dd, GetDesktopWindow(), DDSCL_NORMAL) == DD_OK) {
            v9x_w16l_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = DDSD_CAPS;
            desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            hr = IDirectDraw_CreateSurface(dd, &desc, &primary, 0);
            v9x_w16l_hex("PrimaryCreate", (DWORD)hr);
            if (hr == DD_OK) {
                v9x_w16l_zero(&desc, sizeof(desc));
                desc.dwSize = sizeof(desc);
                v9x_w16l_set_stage("ddraw-lock-primary");
                hr = IDirectDrawSurface_Lock(primary, 0, &desc, DDLOCK_WAIT, 0);
                v9x_w16l_hex("PrimaryLock", (DWORD)hr);
                if (hr == DD_OK) {
                    v9x_w16l_read(&in_lock, "DdLock", lock, confirm, 1);
                    IDirectDrawSurface_Unlock(primary, 0);
                    v9x_w16l_read(&after_unlock, "DdUnlocked", lock, confirm, 1);
                    dd_ok = 1ul;
                }
            }
            v9x_w16l_zero(&desc, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.dwWidth = 64ul;
            desc.dwHeight = 64ul;
            desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
            hr = IDirectDraw_CreateSurface(dd, &desc, &offscreen, 0);
            v9x_w16l_hex("OffscreenCreate", (DWORD)hr);
            if (hr == DD_OK) {
                v9x_w16l_zero(&desc, sizeof(desc));
                desc.dwSize = sizeof(desc);
                v9x_w16l_set_stage("ddraw-lock-nosyslock");
                hr = IDirectDrawSurface_Lock(offscreen, 0, &desc,
                                             DDLOCK_WAIT | DDLOCK_NOSYSLOCK, 0);
                v9x_w16l_hex("OffscreenLockNoSys", (DWORD)hr);
                if (hr == DD_OK) {
                    v9x_w16l_read(&in_nosys, "DdLockNoSys", lock, confirm, 1);
                    IDirectDrawSurface_Unlock(offscreen, 0);
                    nosys_ok = 1ul;
                }
            }
        }
        v9x_w16l_set_stage("ddraw-written");
        v9x_w16l_uint("DdLockDone", dd_ok);
        if (dd_ok) {
            v9x_w16l_write_reading(&in_lock);
            v9x_w16l_write_reading(&after_unlock);
        }
        v9x_w16l_uint("DdLockNoSysDone", nosys_ok);
        if (nosys_ok) {
            v9x_w16l_write_reading(&in_nosys);
        }
        if (offscreen != 0) {
            IDirectDrawSurface_Release(offscreen);
        }
        if (primary != 0) {
            IDirectDrawSurface_Release(primary);
        }
        if (dd != 0) {
            IDirectDraw_Release(dd);
        }
        if (module != 0) {
            FreeLibrary(module);
        }
        if (!dd_ok) {
            ok = 0;
        }
    }

    v9x_w16l_set_stage("done");
    v9x_w16l_text("Result", ok ? "PASS" : "FAIL");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_W16L_INI);
    ExitProcess(ok ? 0u : 1u);
}
